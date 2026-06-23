#pragma once
#include <cstdint>//uint32_t
#include <functional>
#include <unistd.h>
#include <functional>
#include <sys/epoll.h>
#include <string>

class Channel
{
public:
    Channel(int fd);
    ~Channel();

private:
    int fd_;

    uint32_t events_;
    uint32_t revents_;

    std::function<void()> readcallback_;
    std::function<void()> writecallback_;
    std::function<void()> closecallback_;

public:
    void setEvents(uint32_t events);
    void setRevents(uint32_t events);
    void setreadCallBack(std::function<void()> read);
    void setwriteCallBack(std::function<void()> write);
    void setcloseCallBack(std::function<void()> close);
    void callBack(uint32_t event);
    int getFd()const;//给我tventloop使用

};