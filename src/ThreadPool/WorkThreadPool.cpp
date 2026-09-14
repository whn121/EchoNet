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
    while (!stop_) 
    {
        Task task = queue_.pop();  // 阻塞获取任务
        auto conn = task.conn_;
        if (!conn) continue;       // 空任务，可能为停止信号
        
        // 用该连接的锁串行处理
        std::lock_guard<std::mutex> lock(conn->getBusinessMutex());

        // 从any中取出HttpRequest
        auto msg = std::any_cast<MyMessage>(task.message_);
        ChatService::instance().handleMessage(task.conn_, msg);
    }
}

void WorkThreadPool::stop()
{
    bool expected = false;
    if (!stop_.compare_exchange_strong (expected, true)) return;
    queue_.stop();  // 唤醒所有等待线程
    for (auto& t : threads_) if (t.joinable()) t.join();
}