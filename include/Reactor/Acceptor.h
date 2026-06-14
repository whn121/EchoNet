#pragma once
#include "EventLoop.h"
#include <memory>
#include "Net/Connection.h"

class Acceptor
{
public:
    Acceptor(int fd, EventLoop* loop);

private:
    int fd_;
    EventLoop* loop_;
    std::unique_ptr<Channel> channel_;
    std::vector<std::unique_ptr<Connection>> connections_; // 持有所有 Connection

public:
    void sublfd();

};

