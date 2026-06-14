#include "TaskQueue.h"
#include <vector>
#include <thread>

class ThreadPool
{
public:
    ThreadPool(int num = 4);
    ~ThreadPool();

private:
    int nums_;
    bool stop_;
    TaskQueue taskqueue_;
    std::vector<std::thread> thread_vector_;

public:
    void submit_(Task wok);
    void worker_();
    void stop();
};