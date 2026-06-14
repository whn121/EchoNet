#include "Reactor/Channel.h"
#include "Logger/logger.h"

Channel::Channel (int fd)
{
    fd_ = fd;
}

Channel::~Channel () = default;

void Channel::setreadCallBack (std::function<void()> read)
{
    readCallBack_ = read;
}

void Channel::setwriteCallBack (std::function<void()> write)
{
    writeCallBack_ = write;
}

void Channel::callBack(uint32_t event)
{
    INFO("进入callback");
    if (event & (EPOLLERR | EPOLLHUP))
    {
        // 直接关闭
        INFO("callback里的推出");
        return;
    }
    if (event & EPOLLIN)
    {
        INFO("进判断event");
        if (readCallBack_) readCallBack_();
    }
    else if (event & EPOLLOUT)
    {
        if (writeCallBack_) writeCallBack_();
    }
}

int Channel::getFd () const
{
    return fd_;
}

uint32_t Channel::getEvent() const
{
    return event_;
}
void Channel::setEvent(uint32_t event)
{
    event_ = event;
}