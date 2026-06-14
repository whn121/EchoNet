#include "Net/Connection.h"
#include "Logger/logger.h"

Connection::Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop) : afd_ (afd), loop_ (loop)
{
    channel_ = move(channel);
    channel_->setEvent(EPOLLIN);
    channel_->setreadCallBack ([this]{this->read();});//因为有默认this参数所以不匹配,要用lambda表达式
    channel_->setwriteCallBack([this]{this->write();});
}

Connection::~Connection()
{
    loop_ -> removeChannel(channel_.get());
    if (afd_ > 0)
    close(afd_);
}

void Connection::read()
{
    INFO("进入read");
    char buf[1024] = {};
    int n = recv(afd_, buf, sizeof(buf), 0);
    if (n == 0)
    {
        // 对端关闭
        loop_->removeChannel(channel_.get());
        close(afd_);
        afd_ = -1;
        return;
    }
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EINTR)
            return;

        // 错误关闭
        loop_->removeChannel(channel_.get());
        close(afd_);
        afd_ = -1;
        return;
    }

    inBuffer_.append(buf, n);

    INFO("读到了");
}

void Connection::write()
{

    char var[1024] = {};
    if (inBuffer_.getMessage(std::string(var)))
    {
        int n = std::string (var).size();
        outBuffer_.append(var, n);
        send(afd_, var, n, 0);//.data()返回vector和string底层指针.get()返回智能指针的
    }
}

bool Connection::getMessage (std::string& mess)
{
    std::string buf;
    if (outBuffer_.getMessage(buf))
    {
        mess = move(buf);
        return true;
    }
    return false;
}

Channel* Connection::getChannel()
{
    return channel_.get();
}