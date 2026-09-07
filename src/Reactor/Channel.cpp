#include "Reactor/Channel.h"
#include "Logger/logger.h"

Channel::Channel(int fd) : fd_(fd) {}
Channel::~Channel() = default;

void Channel::handleEvent(uint32_t events) 
{
    // 错误或挂起优先处理
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) 
    {
        LOG_WARN("Channel error/hup/rdhup on fd=" + std::to_string(fd_) + " events=" + std::to_string(events));
        if (closecallback_) closecallback_();
        return;
    }
    if (events & EPOLLIN)  { if (readcallback_) readcallback_(); }
    if (events & EPOLLOUT) { if (writecallback_) writecallback_(); }
}

void Channel::setEvents (uint32_t events) { events_ = events; }
uint32_t Channel::getEvents () const { return events_; }
void Channel::setRevents (uint32_t revents) { revents_ = revents; }
uint32_t Channel::getRevents () const { return revents_; }
void Channel::setreadCallBack (std::function<void()> cb) { readcallback_ = cb; }
void Channel::setwriteCallBack (std::function<void()> cb) { writecallback_ = cb; }
void Channel::setcloseCallBack (std::function<void()> cb) { closecallback_ = cb; }
void Channel::enableReading () { events_ |= EPOLLIN; }
void Channel::enableWriting () { events_ |= EPOLLOUT; }
void Channel::disableWriting () { events_ &= ~EPOLLOUT; }
void Channel::disableAll () { events_ = 0; }
bool Channel::isReading () const { return events_ & EPOLLIN; }
bool Channel::isWriting () const { return events_ & EPOLLOUT; }