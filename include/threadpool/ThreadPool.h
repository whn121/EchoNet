#pragma one
#include <thread>
#include <vector>
#include "TaskQueue.h"

class ThreadPool
{
public:
    ThreadPool(int num = 4);
    ~ThreadPool();

private:
    int num_;
    bool stop_;
    std::vector<std::thread> thread_array_;
    TaskQueue task_queue_;

    void woker_for();

public:
    void submit(task val);
    void stop();
};

