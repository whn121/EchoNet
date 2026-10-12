#include "Reactor/Channel.h"
#include "Logger/AsyncLogger.h"

Channel::Channel(int fd) : fd_(fd) {}
Channel::~Channel() = default;

void Channel::handleEvent(uint32_t events)
{
    // 真正的socket错误, 走统一关闭流程
    if (events & EPOLLERR)
    {
        LOG_WARN("Channel error on fd=" + std::to_string(fd_) + " events=" + std::to_string(events));

        if (closecallback_) closecallback_();
        return;
    }

    // EPOLLRDHUP：对端关闭写端，转成 EPOLLIN，让 read() 读到 0 再走 close
    // 这样不会丢掉对端在 close 前发的最后一批数据
    if (events & (EPOLLRDHUP | EPOLLHUP))
    {
        events |= EPOLLIN;
    }

    if (events & EPOLLIN)
    {
        if (readcallback_)
        {
            readcallback_();
        }
        else if ((events & EPOLLHUP) && closecallback_)
        {
            closecallback_();
            return;
        }
    }

    if (events & EPOLLOUT)
    {
        if (writecallback_)
        {
            writecallback_();
        }
    }
}

void Channel::setEvents (uint32_t events) { events_ = events; }
uint32_t Channel::getEvents () const { return events_; }
void Channel::setRevents (uint32_t revents) { revents_ = revents; }
uint32_t Channel::getRevents () const { return revents_; }
void Channel::setreadCallBack (std::function<void()> cb) { readcallback_ = cb; }
void Channel::setwriteCallBack (std::function<void()> cb) { writecallback_ = cb; }
void Channel::setcloseCallBack (std::function<void()> cb) { closecallback_ = cb; }
void Channel::enableReading () { events_ |= EPOLLIN; }
void Channel::disableReading () { events_ &= ~EPOLLIN; } // 清理可读其他保留
void Channel::enableWriting () { events_ |= EPOLLOUT; }
void Channel::disableWriting () { events_ &= ~EPOLLOUT; }
void Channel::disableAll () { events_ = 0; }
bool Channel::isReading () const { return events_ & EPOLLIN; }
bool Channel::isWriting () const { return events_ & EPOLLOUT; }