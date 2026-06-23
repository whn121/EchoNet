#include "Reactor/Channel.h"

Channel::Channel (int fd)
{
    fd_ = fd;
}

Channel::~Channel () = default;

void Channel::callBack(uint32_t event)
{
    if (event & (EPOLLERR | EPOLLHUP))
    {
        if (closecallback_) closecallback_();
        return;
    }

    if (event & EPOLLIN)
    {
        if (readcallback_) readcallback_();
    }

    if (event & EPOLLOUT)
    {
        if (writecallback_) writecallback_();
    }
}

void Channel::setEvents (uint32_t events)
{
    events_ = events;
}

void Channel::setRevents (uint32_t events)
{
    revents_ = events;
}

void Channel::setreadCallBack (std::function<void()> read)
{
    readcallback_ = read;
}

void Channel::setwriteCallBack (std::function<void()> write)
{
    writecallback_ = write;
}

void Channel::setcloseCallBack (std::function<void()> close)
{
    closecallback_ = close;
}

int Channel::getFd () const
{
    return fd_;
}
