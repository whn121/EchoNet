#pragma once

#include <sys/epoll.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "Channel.h"

class Connection; // 前向声明

// 事件循环（每个线程一个），封装epoll，驱动所有Channel回调
class EventLoop {
public:
    EventLoop();
    ~EventLoop();
    void loop();                                           // 主循环
    void updateChannel(Channel* ch, uint32_t events);      // 添加Channel
    void removeChannel(Channel* ch);                       // 移除Channel
    void updateConnection(std::shared_ptr<Connection> conn); // 持有Connection
    void removeConnection(int fd);                         // 移除并关闭
    void updateChannelEvent(Channel* ch, uint32_t events); // 修改监听事件
    void runInLoop(std::function<void()> cb);              // 线程安全投递任务
    void wakeUp();                                         // 唤醒epoll
    void stop();                                           // 停止循环
private:
    int efd_;                             // epoll实例
    std::vector<epoll_event> events_;     // 就绪事件数组
    std::unordered_map<int, Channel*> channels_;            // fd -> Channel
    std::unordered_map<int, std::shared_ptr<Connection>> connections_; // 持有连接
    bool stop_ = false;

    int wakeup_fd_;                       // eventfd，用于唤醒
    std::vector<std::function<void()>> task_queue_; // 待执行的回调
    std::mutex mtx_;
};