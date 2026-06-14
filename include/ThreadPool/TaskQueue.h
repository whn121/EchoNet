#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

using Task = std::function<void()>;

class TaskQueue
{
public:
    TaskQueue();
    ~TaskQueue();

private:
    bool stop_; // 要知道线程池是否停止要不没法启动全部线程条件变量不满足
    std::queue<Task> taskqueue_; 
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void push_(Task wok);
    Task pop_();
    void cvnotify_all();
    void setstop();

};