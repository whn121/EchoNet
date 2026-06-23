#pragma once
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "ThreadPool/Task.h"

class TaskQueue
{
public:
    TaskQueue();
    ~TaskQueue();

private:
    bool stop_; // 要知道线程池是否停止要不没法启动全部线程条件变量不满足
    std::queue<Task> taskqueue_; 
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void push_(Task work);
    Task pop_();
    void cvnotify_all();
    void setstop();

};