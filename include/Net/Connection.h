#pragma once
#include "Net/Buffer.h"
#include <unistd.h>
#include <sys/socket.h> 
#include "Reactor/Channel.h"
#include <memory>
#include "Reactor/EventLoop.h"

class Connection
{
public:
    Connection(int afd, std::unique_ptr<Channel> cahnnel, EventLoop* loop);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator= (const Connection&) = delete;

private:
    int afd_;
    EventLoop* loop_;
    Buffer inBuffer_;
    Buffer outBuffer_;
    std::unique_ptr<Channel> channel_;

public:
    void read();
    void write();
    bool getMessage(std::string& mess);
    Channel* getChannel();
};
