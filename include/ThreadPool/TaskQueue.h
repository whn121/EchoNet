#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "Task.h"

// 线程安全的任务队列（阻塞队列）
class TaskQueue 
{
public:
    // 队列停止后拒绝新的任务: 返回false时不移动task
    void push(Task task);
    // 尝试提交任务。
    // 队列停止后返回 false，且不移动 task，调用方可以处理被拒绝的任务。
    bool tryPush(Task&& task);
    Task pop();                 // 阻塞直到有任务或停止
    void stop();                // 唤醒所有等待并停止
private:
    std::queue<Task> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};