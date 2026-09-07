#pragma once

#include <sys/epoll.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "Channel.h"
#include <atomic> //原子操作
#include <thread> //亲合度检测


class Connection; // 前向声明

// 事件循环（每个线程一个），封装epoll，驱动所有Channel回调
class EventLoop 
{
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

    bool isInLoopThread () const; //判断是否在所属线程
    void assertInLoopThread () const; //断言当前线程是否为所属线程
    
private:
    int efd_;                             // epoll实例
    std::vector<epoll_event> events_;     // 就绪事件数组
    std::unordered_map<int, Channel*> channels_;            // fd -> Channel
    std::unordered_map<int, std::shared_ptr<Connection>> connections_; // 持有连接
    std::atomic<bool> stop_ {false}; //使用原子操作避免竞争

    int wakeup_fd_;                       // eventfd，用于唤醒
    std::vector<std::function<void()>> task_queue_; // 待执行的回调
    std::mutex mtx_;

    //暂存即将销毁的连接，延迟到本次事件循环末尾释放
    //connections_ 中可能持有该 Connection 的最后一个 shared_ptr。一旦 erase，引用计数归零，Connection 被立即析构。
    //但此时你仍然在 Connection::read() 函数内部，这个函数是通过 Channel 的回调调用的，
    //而 Channel 是 Connection 的成员（unique_ptr<Channel>
    std::vector<std::shared_ptr<Connection>> pendingConnections_;

    std::thread::id owner_thread_id_; //所属线程ID

};