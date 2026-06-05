#pragma one
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

using task = std::function<void()>;

class TaskQueue
{
public:
    TaskQueue();
    ~TaskQueue();

private:
    std::queue<task> TQ_;
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void tpush(task& x);
    task tpop();
    bool tempty();
    void mynotify_all();
};