#pragma once
#include "EventLoop.h"
#include <memory>
#include "Net/Connection.h"
#include "ThreadPool/IoThreadPool.h"
#include <functional>


class Acceptor
{
public:
    Acceptor(int fd, EventLoop* loop, IoThreadPool* iopool);

private:
    int fd_;
    EventLoop* loop_;
    std::unique_ptr<Channel> channel_;
    IoThreadPool* iopool_;
    std::function<void(int)> iopoolsubmitcallback_;

public:
    void sublfd();
    void listen();
    void setCallBack(std::function<void(int)>);

};

