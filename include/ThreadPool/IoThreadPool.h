#pragma once
#include "TaskQueue.h"
#include "Reactor/EventLoop.h"
#include "vector"
#include "thread"
#include "Net/Connection.h"
#include <atomic>


class IoThreadPool
{
public:
    IoThreadPool(int num = 4);
    ~IoThreadPool();

private:
    int efd_;
    std::vector<std::unique_ptr<EventLoop>> loops_;
    bool stop_;
    int nums_;
    std::vector<std::thread> thread_;

    std::function<void(Task)> giveconncallback_;

    std::atomic<size_t> next_{0};//原子计数器,专门用在多线程里保证安全的

public:
    void stop();
    void submit(int afd);
    void worker(int i);
    
    void setCallBack(std::function<void(Task)>);
    std::function<void(Task)> giveconn ();

};