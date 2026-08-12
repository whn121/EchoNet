#include "Reactor/Acceptor.h"
#include "ThreadPool/IoThreadPool.h"
#include <fcntl.h>      // fcntl
#include <unistd.h>     // close 等（如有需要）

// 构造函数：保存监听 fd、事件循环、IO 线程池，并创建对应的 Channel
Acceptor::Acceptor(int fd, EventLoop* loop, IoThreadPool* pool)
    : fd_(fd), loop_(loop), pool_(pool),
      channel_(std::make_unique<Channel>(fd))   // 为监听 fd 创建 Channel
{}

// 开始监听：将 handleAccept 注册为读事件回调，并加入 epoll
void Acceptor::listen() {
    channel_->setreadCallBack([this] { handleAccept(); });  // 绑定回调
    loop_->updateChannel(channel_.get(), EPOLLIN);           // 注册到 epoll，监听读事件
}

// 设置新连接到来时的外部回调（用于将新连接分发给 IO 线程池）
void Acceptor::setCallBack(std::function<void(int)> cb) {
    newConnectionCallback_ = cb;
}

// 处理新连接：accept 获得 fd，设为非阻塞，然后通过回调分发
void Acceptor::handleAccept() {
    sockaddr_in addr{};                    // 客户端地址结构
    socklen_t len = sizeof(addr);
    int afd = accept(fd_, (sockaddr*)&addr, &len); // 接受连接
    if (afd < 0) return;                  // 出错则忽略

    // 将新连接设置为非阻塞模式（配合 epoll 必须非阻塞）
    int flags = fcntl(afd, F_GETFL, 0);
    fcntl(afd, F_SETFL, flags | O_NONBLOCK);

    // 调用外部设置的回调，把新 fd 传递给 IoThreadPool
    if (newConnectionCallback_) {
        newConnectionCallback_(afd);
    }
}