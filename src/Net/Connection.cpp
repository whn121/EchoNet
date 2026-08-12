#include "Net/Connection.h"
#include "Logger/logger.h"

// 构造函数：转移 Channel 和 Protocol 的所有权，保存 EventLoop 指针
Connection::Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop,
                       std::unique_ptr<Protocol> protocol)
    : afd_(afd), loop_(loop), channel_(std::move(channel)), protocol_(std::move(protocol)) {}

Connection::~Connection() {
    if (afd_ > 0) close(afd_);   // 关闭套接字
}

// ---------- 读事件处理 ----------
void Connection::read() {
    char buf[1024] = {};
    int n = recv(afd_, buf, sizeof(buf), 0);  // 非阻塞读取
    if (n <= 0) {
        // 出错或对端关闭，触发关闭回调
        if (closeCallBack_) closeCallBack_(afd_);
        return;
    }

    inBuffer_.bufferAppend(buf, n);   // 追加到输入缓冲区

    // 让协议对象解析数据
    ParseResult res = protocol_->parse(inBuffer_);

    if (res == ParseResult::OK) {
        // 解析成功，封装任务投递给业务线程池
        Task task;
        task.conn_ = shared_from_this();          // 延长 Connection 生命周期
        task.message_ = protocol_->getMessage();  // 取出解析好的请求消息
        if (worksumbitcallback_) worksumbitcallback_(task);
        protocol_->reset();  // 重置协议状态，准备解析下一个请求
    } else if (res == ParseResult::ERROR) {
        // 协议错误：尝试获取协议层预置的错误响应
        auto errResp = protocol_->getErrorResponse();
        if (errResp.has_value()) {
            sendResponse(errResp.value());   // 发送错误响应给客户端
            protocol_->reset();
        } else {
            // 协议层没有提供错误响应，直接关闭连接
            if (closeCallBack_) closeCallBack_(afd_);
        }
    }
    // NEED_MORE：数据不足，不做任何操作，等待下一次读事件
}

// ---------- 写事件处理 ----------
void Connection::write() {
    while (outBuffer_.getreadable() > 0) {
        size_t n = send(afd_, outBuffer_.peek(), outBuffer_.getreadable(), 0);
        if (n == 0) {
            if (closeCallBack_) closeCallBack_(afd_); // 对端关闭
            return;
        }
        if (n > 0) {
            outBuffer_.goReadPtr(n);   // 发送成功，移动读指针
        } else {
            if (errno == EAGAIN || errno == EINTR) break; // 资源暂时不可用，等待下次
            if (closeCallBack_) closeCallBack_(afd_);     // 真正错误，关闭连接
            return;
        }
    }

    // 如果发送缓冲区已空，取消写事件监听（不再关注 EPOLLOUT），只关注读
    if (outBuffer_.getreadable() == 0) {
        loop_->updateChannelEvent(channel_.get(), EPOLLIN);
    }
}

// ---------- 发送响应 ----------
// 接收任意类型的响应（any），由协议编码后放入输出缓冲区，并激活写事件
void Connection::sendResponse(const std::any& response) {
    std::string data = protocol_->encode(response);      // 协议编码
    outBuffer_.bufferAppend(data.data(), data.size());   // 放入输出缓冲区

    // 通过 runInLoop 保证线程安全地修改监听事件，激活 EPOLLOUT
    loop_->runInLoop([this] {
        loop_->updateChannelEvent(channel_.get(), EPOLLIN | EPOLLOUT);
    });
}

Channel* Connection::getChannel() {
    return channel_.get();
}

void Connection::setcloseCallback(std::function<void(int)> close) {
    closeCallBack_ = close;
}

void Connection::myClose() {
    if (closeCallBack_) closeCallBack_(afd_);
}

// 初始化：将 Channel 的回调绑定到 Connection 的成员函数
void Connection::init() {
    auto self = shared_from_this();   // 获得自身的 shared_ptr，防止回调中对象被释放

    channel_->setreadCallBack([self] { self->read(); });    // 读就绪
    channel_->setwriteCallBack([self] { self->write(); });  // 写就绪
    channel_->setcloseCallBack([self] { self->myClose(); });// 挂起/错误
}

void Connection::setCallBack(std::function<void(Task)> callback) {
    worksumbitcallback_ = callback;   // 设置业务处理回调
}