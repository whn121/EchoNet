#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "Task.h"

// 线程安全的任务队列（阻塞队列）
class TaskQueue {
public:
    void push(Task task);
    Task pop();                 // 阻塞直到有任务或停止
    void stop();                // 唤醒所有等待并停止
private:
    std::queue<Task> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};