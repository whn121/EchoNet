#include "Net/Connection.h"
#include "Logger/logger.h"


// 构造函数：转移 Channel 和 Protocol 的所有权，保存 EventLoop 指针
Connection::Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop,
                       std::unique_ptr<Protocol> protocol)
    : afd_(afd), loop_(loop), channel_(std::move(channel)), protocol_(std::move(protocol)) {}

Connection::~Connection() 
{
    if (afd_ != -1) close(afd_);   // 关闭套接字
}

// ---------- 读事件处理 ----------
void Connection::read() 
{
    char buf[1024] = {};

    // 循环读取，直到内核缓冲区没有数据（EAGAIN）或发生错误
    while (true)
    {
        ssize_t n = recv(afd_, buf, sizeof(buf), 0);  // 非阻塞读取

        if (n > 0) 
        {
            inBuffer_.bufferAppend(buf, n);   // 追加到输入缓冲区
            //继续循环
        }
        else if (n == 0)
        {
            //对端正常关闭
            handleClose();
            return;
        }
        else
        {
            // n < 0,根据errno判断
            if (errno == EINTR) continue; //信号中断,立即重试
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; //内核缓冲区空了, 等待下一次epoll事件
            // 其他错误，关闭连接
            handleClose();
            return;
        }
    }
    // 循环解析所有可解析的完整请求
    while (true)
    {
        ParseResult res = protocol_->parse(inBuffer_);

        if (res == ParseResult::OK) 
        {
            // 解析成功，封装任务投递给业务线程池
            Task task;
            task.conn_ = shared_from_this();          // 延长 Connection 生命周期
            task.message_ = protocol_->getMessage();  // 取出解析好的请求消息
            if (worksumbitcallback_) worksumbitcallback_(task);
            protocol_->reset();  // 重置协议状态，准备解析下一个请求
        } 
        else if (res == ParseResult::ERROR) 
        {
            // 协议错误：尝试获取协议层预置的错误响应
            auto errResp = protocol_->getErrorResponse();
            if (errResp.has_value()) 
            {
                sendResponse(errResp.value());   // 发送错误响应给客户端
                protocol_->reset();
            } 
            else 
            {
                // 协议层没有提供错误响应，直接关闭连接
                handleClose();
            }
            break;
        }
        else
        {
            //NEED_MORE
            break;
        }
    }
}

// ---------- 写事件处理 ----------
void Connection::write() 
{
    while (outBuffer_.getreadable() > 0) 
    {
        ssize_t n = send(afd_, outBuffer_.peek(), outBuffer_.getreadable(), 0);
        if (n > 0) 
        {
            outBuffer_.goReadPtr(n);   // 发送成功，移动读指针
        }
        else if (n == 0) 
        {
            handleClose();
            return;
        }
        else 
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 资源暂时不可用，等待下次
            handleClose();
            return;
        }
    }

    // 如果发送缓冲区已空，取消写事件监听（不再关注 EPOLLOUT），只关注读
    if (outBuffer_.getreadable() == 0) 
    {
        loop_->updateChannelEvent(channel_.get(), EPOLLIN);
    }
}

// ---------- 发送响应 ----------
// 接收任意类型的响应（any），由协议编码后放入输出缓冲区，并激活写事件
void Connection::sendResponse(const std::any& response) 
{
    //编码可以放在当前线程（业务线程），因为 protocol_ 是只读的，不涉及 I/O 状态
    std::string data = protocol_->encode(response);      // 协议编码
    //weak_ptr引用,确保执行期间存活,不然如果sendResponse被工作线程调用时,链接可能已经关闭并析构,回调执行时访问this,这时是空指针
    std::weak_ptr<Connection> weakself = shared_from_this();

    // 通过 runInLoop 保证线程安全地修改监听事件，激活 EPOLLOUT,将 outBuffer 追加和 epoll 事件修改打包投递到 I/O 线程执行
    loop_->runInLoop([weakself, data = std::move (data)] 
    {
        auto self = weakself.lock ();
        if (!self) return;

        // 以下操作都在 I/O 线程执行，与 write() 同线程，无并发问题
        self -> outBuffer_.bufferAppend(data.data(), data.size());   // 放入输出缓冲区
        self -> loop_->updateChannelEvent(self -> channel_.get(), EPOLLIN | EPOLLOUT);
    });

}

Channel* Connection::getChannel() 
{
    return channel_.get();
}

void Connection::setcloseCallback(std::function<void(int)> close) 
{
    closeCallBack_ = close;
}

void Connection::myClose() 
{
    handleClose();
}

// 初始化：将 Channel 的回调绑定到 Connection 的成员函数
void Connection::init() 
{
    //不能活的强引用,要不然循环引用了,无法析构计数不会归零
    std::weak_ptr<Connection> weakself = shared_from_this();   // 获得自身的weak_ptr，防止回调中对象被释放

    channel_->setreadCallBack([weakself] { if (auto self = weakself.lock()) { self -> read(); } });      // 读就绪,lock尝试升级为临时强引用,回调结束析构
    channel_->setwriteCallBack([weakself] { if (auto self = weakself.lock()) { self -> write(); } });    // 写就绪,lock尝试升级为临时强引用,回调结束析构
    channel_->setcloseCallBack([weakself] { if (auto self = weakself.lock()) { self -> myClose(); } });  // 挂起/错误,lock尝试升级为临时强引用,回调结束析构
}

void Connection::setCallBack(std::function<void(Task)> callback) 
{
    worksumbitcallback_ = callback;   // 设置业务处理回调
}

void Connection::handleClose() {
    if (closing_) return;          // 已经关闭，防止重复
    closing_ = true;
    if (closeCallBack_) {
        closeCallBack_(afd_);
    }
}
