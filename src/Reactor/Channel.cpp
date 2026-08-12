#include "Reactor/Channel.h"

Channel::Channel(int fd) : fd_(fd) {}
Channel::~Channel() = default;

void Channel::callBack(uint32_t event) {
    // 错误或挂起优先处理
    if (event & (EPOLLERR | EPOLLHUP)) {
        if (closecallback_) closecallback_();
        return;
    }
    if (event & EPOLLIN)  { if (readcallback_) readcallback_(); }
    if (event & EPOLLOUT) { if (writecallback_) writecallback_(); }
}

void Channel::setEvents(uint32_t ev) { events_ = ev; }
void Channel::setRevents(uint32_t rev) { revents_ = rev; }
void Channel::setreadCallBack(std::function<void()> cb) { readcallback_ = cb; }
void Channel::setwriteCallBack(std::function<void()> cb) { writecallback_ = cb; }
void Channel::setcloseCallBack(std::function<void()> cb) { closecallback_ = cb; }
int Channel::getFd() const { return fd_; }