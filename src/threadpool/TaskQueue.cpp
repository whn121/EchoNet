#include "threadpool/TaskQueue.h"

TaskQueue::TaskQueue() = default;
TaskQueue::~TaskQueue() = default;

void TaskQueue::tpush(task& x)
{
    {
        std::unique_lock<std::mutex> lock(mtx_);
        TQ_.emplace(x);
    }
    cv_.notify_one();
}

task TaskQueue::tpop()
{
    std::unique_lock<std::mutex> lock (mtx_);
    cv_.wait(lock,[this]{return !TQ_.empty();});
    task front = std::move(TQ_.front());//不会拷贝
    TQ_.pop();
    return front;
}

 bool TaskQueue::tempty()
{
    std::unique_lock<std::mutex> lock(mtx_);
    return TQ_.empty();
}

void TaskQueue::mynotify_all()
{
    cv_.notify_all();
}
