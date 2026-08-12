#pragma once
#include "TaskQueue.h"
#include <vector>
#include <thread>
#include <functional>

// 业务线程池：执行具体的业务逻辑（HttpService::handle）
class WorkThreadPool {
public:
    WorkThreadPool(int num = 4);
    ~WorkThreadPool();
    void submit(Task task);   // 提交任务
    void stop();
private:
    void worker();            // 工作线程主函数
    int num_;
    bool stop_ = false;
    TaskQueue queue_;
    std::vector<std::thread> threads_;
};