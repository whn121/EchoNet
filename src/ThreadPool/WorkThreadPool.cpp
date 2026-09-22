#include "ThreadPool/WorkThreadPool.h"
#include "Net/Connection.h"
#include "HTTP/HttpService.h"
#include <any>
#include "Logger/AsyncLogger.h"
#include "Protocol/MyProtocol.h"
#include "ChatService/ChatService.h"

WorkThreadPool::WorkThreadPool(int num) : num_(num > 0 ? num : 4) 
{
    for (int i = 0; i < num; ++i)
    threads_.emplace_back([this] { worker(); });
}

WorkThreadPool::~WorkThreadPool() { stop(); }

void WorkThreadPool::submit(Task task) 
{
    queue_.push(std::move(task));
}

void WorkThreadPool::worker() 
{
    while (true) 
    {
        Task task = queue_.pop();         // 阻塞获取任务
        
        if (!task.run_)
        {
            // pop() 返回空 Task = 队列空 + 已停止
            // 说明所有任务都处理完了，可以退出
            break;
        }

        task.run_();     
    }
}

void WorkThreadPool::stop()
{
    bool expected = false;
    if (!stop_.compare_exchange_strong (expected, true)) return;

    queue_.stop();  // 唤醒所有等待线程
    
    for (auto& t : threads_) if (t.joinable()) t.join();
}