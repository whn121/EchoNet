#pragma once

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

// 封装一个文件描述符及其事件回调
class Channel 
{
public:
    explicit Channel(int fd); //关闭隐式转换
    ~Channel();
    void setEvents(uint32_t events);            // 设置监听的事件
    uint32_t getEvents() const;

    void setRevents(uint32_t revents);          // epoll返回的就绪事件
    uint32_t getRevents() const;

    void setreadCallBack(std::function<void()> cb);
    void setwriteCallBack(std::function<void()> cb);
    void setcloseCallBack(std::function<void()> cb);

    void handleEvent(uint32_t events);              // 事件分发

    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    bool isWriting() const;
    bool isReading() const;

    int getFd() const {return fd_;};
private:
    int fd_;
    uint32_t events_ = 0;     // 关心的事件
    uint32_t revents_ = 0;    // 实际发生的事件
    std::function<void()> readcallback_;
    std::function<void()> writecallback_;
    std::function<void()> closecallback_;
};