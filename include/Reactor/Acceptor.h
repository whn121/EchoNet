#pragma once
#include "EventLoop.h"
#include <memory>
#include <functional>
#include <netinet/in.h>   // sockaddr_in
#include <sys/socket.h>   // accept

class IoThreadPool;

class Acceptor {
public:
    Acceptor(int fd, EventLoop* loop, IoThreadPool* pool);
    void listen();
    void setCallBack(std::function<void(int)> cb);
private:
    int fd_;
    EventLoop* loop_;
    std::unique_ptr<Channel> channel_;
    IoThreadPool* pool_;
    std::function<void(int)> newConnectionCallback_;   // 新连接回调
    void handleAccept();
};