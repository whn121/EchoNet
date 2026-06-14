#include "Reactor/Acceptor.h"
#include "Net/Socket.h"
#include "Logger/logger.h"

Acceptor::Acceptor(int fd, EventLoop* loop) : 
    channel_(std::make_unique<Channel> (fd)), 
    fd_ (fd), loop_ (loop)
{
    channel_ -> setEvent(EPOLLIN);
    INFO("进rcceptor构造了");
    channel_ -> setreadCallBack([this]{this -> sublfd();});
    loop_ -> updateChannel(channel_.get());
}

void Acceptor::sublfd()
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int afd = accept(fd_, (sockaddr*)&addr, &len);
    if (afd < 0) return;

    int flag = fcntl(afd, F_GETFL, 0);
    fcntl(afd, F_SETFL, flag | O_NONBLOCK);

    auto newChannel = std::make_unique<Channel>(afd);
    newChannel->setEvent(EPOLLIN);
    
    auto conn = std::make_unique<Connection>(afd, std::move(newChannel), loop_);
    loop_->updateChannel(conn->getChannel());
    connections_.push_back(std::move(conn)); // 持有
    INFO("链接成功");

}