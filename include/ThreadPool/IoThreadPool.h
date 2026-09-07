#pragma once
#include "Reactor/EventLoop.h"
#include <vector>
#include <thread>
#include <memory>
#include <atomic> //对他的读写,自增,自减,操作是一起的
#include "Task.h"
#include <atomic>


// IO线程池：管理一组子EventLoop，每个循环运行在独立线程
class IoThreadPool 
{
public:
    IoThreadPool(int num = 4);
    ~IoThreadPool();
    void submit(int afd);                              // 分发新连接
    void setCallBack(std::function<void(Task)> cb);    // 设置工作线程池回调
    std::function<void(Task)> getCallback();           // 获取回调
    void stop();

    void setbuildconn (std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)>);

private:
    void worker(int idx);                              // 线程工作函数
    std::vector<std::unique_ptr<EventLoop>> loops_;    // 子EventLoop
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_ {false};
    int num_;
    std::function<void(Task)> workCallback_;           // 投递给工作线程池
    std::atomic<size_t> next_{0};                      // 轮询计数器
    //{}给成员就地初始化列表

    //为了解耦io不知道协议类型,在acceptor里完成io执行回调
    std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)> buildconn_;

};