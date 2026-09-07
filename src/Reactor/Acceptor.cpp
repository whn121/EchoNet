#include "Reactor/Acceptor.h"
#include "ThreadPool/IoThreadPool.h"
#include <fcntl.h>      // fcntl
#include <unistd.h>     // close 等（如有需要）
#include <netinet/tcp.h> //关闭Nagle算法(防止大量小包等待数据满了再发)


// 构造函数：保存监听 fd、事件循环、IO 线程池，并创建对应的 Channel
Acceptor::Acceptor(int fd, EventLoop* loop, IoThreadPool* pool)
    : fd_(fd), loop_(loop), pool_(pool),
      channel_(std::make_unique<Channel>(fd))   // 为监听 fd 创建 Channel
{}

// 开始监听：将 handleAccept 注册为读事件回调，并加入 epoll
void Acceptor::listen() 
{
    channel_->setreadCallBack([this] { handleAccept(); });  // 绑定回调
    loop_->updateChannel(channel_.get(), EPOLLIN);           // 注册到 epoll，监听读事件
}

// 设置新连接到来时的外部回调（用于将新连接分发给 IO 线程池）
void Acceptor::setCallBack(std::function<void(int)> cb) 
{
    newConnectionCallback_ = cb;
}

// 处理新连接：accept 获得 fd，设为非阻塞，然后通过回调分发
//要循环acppet知道EAGAIN
void Acceptor::handleAccept() 
{
    sockaddr_in addr{};                    // 客户端地址结构
    socklen_t len = sizeof(addr);

    while (true) 
    {
        int afd = accept4(fd_, (sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);

        int one = 1;
        setsockopt(afd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (afd < 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                break;   // 内核没有更多连接了
            }
            if (errno == EINTR) 
            {
                continue;   // 被信号中断，重试
            }
            // 其他错误，可以记录日志（如果你有）
            break;
        }

        if (newConnectionCallback_) 
        {
            newConnectionCallback_(afd);
        }
    }
}