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
    uint32_t event_;
    std::function<void()> readCallBack_;
    std::function<void()> writeCallBack_;
    std::function<void()> closeCallBack_;

public:
    void setEvent(uint32_t event);
    void setreadCallBack(std::function<void()> read);
    void setwriteCallBack(std::function<void()> write);
    void callBack(uint32_t event);
    int getFd()const;//给我tventloop使用
    uint32_t getEvent()const;//给我tventloop使用

};