#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>
#include <sys/epoll.h>
#include "Channel.h"
#include <vector>
#include <unordered_map>

class EventLoop //不应该持有channnel,只应该观察,不能用智能指针,他们持有智能指针也不会析构;
{
public:
    EventLoop();
    ~EventLoop();

private:
    int efd_;
    std::vector<epoll_event> event_;
    std::unordered_map <int, Channel*> channel_;
    bool stop_;

public:
    void loop();
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

};
