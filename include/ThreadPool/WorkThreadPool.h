#pragma once
#include "TaskQueue.h"
#include <vector>
#include <thread>

class WorkThreadPool
{
public:
    WorkThreadPool(int num = 4);
    ~WorkThreadPool();

private:
    int nums_;
    bool stop_;
    TaskQueue taskqueue_;
    std::vector<std::thread> thread_vector_;

public:
    void submit_(Task work);
    void worker_();
    void stop();
    
};