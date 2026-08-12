#include "ThreadPool/TaskQueue.h"

void TaskQueue::push(Task task) {
    std::lock_guard<std::mutex> lock(mtx_);
    queue_.push(std::move(task));
    cv_.notify_one();   // 唤醒一个等待的线程
}

Task TaskQueue::pop() {
    std::unique_lock<std::mutex> lock(mtx_);
    // 等待条件：队列非空或已停止
    cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
    if (stop_ && queue_.empty()) return {};  // 停止且队列空返回空Task
    Task task = std::move(queue_.front());
    queue_.pop();
    return task;
}

void TaskQueue::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    stop_ = true;
    cv_.notify_all();  // 唤醒所有线程以便退出
}