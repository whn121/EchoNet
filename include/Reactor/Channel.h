#pragma once

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

// 封装一个文件描述符及其事件回调
class Channel {
public:
    Channel(int fd);
    ~Channel();
    void setEvents(uint32_t events);            // 设置监听的事件
    void setRevents(uint32_t revents);          // epoll返回的就绪事件
    void setreadCallBack(std::function<void()> cb);
    void setwriteCallBack(std::function<void()> cb);
    void setcloseCallBack(std::function<void()> cb);
    void callBack(uint32_t event);              // 事件分发
    int getFd() const;
private:
    int fd_;
    uint32_t events_ = 0;     // 关心的事件
    uint32_t revents_ = 0;    // 实际发生的事件
    std::function<void()> readcallback_;
    std::function<void()> writecallback_;
    std::function<void()> closecallback_;
};