#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>
#include <sys/epoll.h>
#include "Channel.h"
#include <vector>
#include <unordered_map>
#include <mutex>


class Connection;  // 前向声明

class EventLoop //不应该持有channnel,只应该观察,不能用智能指针,别人持有智能指针也不会析构;
{
public:
    EventLoop();
    ~EventLoop();

private:
    int efd_;
    std::vector<epoll_event> event_;
    std::unordered_map <int, Channel*> channel_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_; // 持有所有 Connection
    bool stop_;

    int wakeup_fd_; //被epoll监听的门铃,一修改wait就会返回嘛
    std::vector<std::function<void()>> task_vector_;
    std::mutex mtx_; 

public:
    void loop();
    void updateChannel(Channel* channel, uint32_t events);
    void removeChannel(Channel* channel);
    void updateConnection(std::shared_ptr<Connection> connection);
    void removeConnection(int fd);
    void updateChannelEvent (Channel* channel, uint32_t events);

    void runInLoop(std::function<void()> cb);
    void wakeUp();
    void stop();    

};
