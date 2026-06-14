#include "ThreadPool/TaskQueue.h"

TaskQueue::TaskQueue()
{
    stop_ = false;
}

TaskQueue::~TaskQueue() = default;

void TaskQueue::push_ (Task wok)
{
    std::unique_lock <std::mutex> lock(mtx_);
    taskqueue_.emplace (move(wok));//传的时候也是拷贝
    cv_.notify_one();
}

Task TaskQueue::pop_()
{
    std::unique_lock <std::mutex> lock(mtx_);
    cv_.wait(lock, [this]{return stop_ || !taskqueue_.empty();});//线程池推出问题
    if (stop_ && taskqueue_.empty())
    return {}; // 线程推出工作;
    Task task = move(taskqueue_.front());//front返回的引用不是临时变量
    taskqueue_.pop();
    return task;
}

void TaskQueue::cvnotify_all()
{
    cv_.notify_all();
}

void TaskQueue::setstop()
{
    stop_ = true;
}