#include "Reactor/Acceptor.h"
#include "Net/Socket.h"
#include "Logger/logger.h"

Acceptor::Acceptor (int fd, EventLoop* loop, IoThreadPool* iopool) : 
    channel_(std::make_unique<Channel> (fd)), 
    fd_ (fd), loop_ (loop), iopool_ (iopool) {}


void Acceptor::sublfd ()
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int afd = accept(fd_, (sockaddr*)&addr, &len);
    if (afd < 0) return;

    int flag = fcntl(afd, F_GETFL, 0);
    fcntl(afd, F_SETFL, flag | O_NONBLOCK);

    if (iopoolsubmitcallback_) iopoolsubmitcallback_ (afd);
}

void Acceptor::listen ()
{
    channel_ -> setreadCallBack([this]{this -> sublfd();});
    loop_ -> updateChannel(channel_.get(), EPOLLIN);
}

void Acceptor::setCallBack (std::function<void(int)> callback)
{
    iopoolsubmitcallback_ = callback;
}