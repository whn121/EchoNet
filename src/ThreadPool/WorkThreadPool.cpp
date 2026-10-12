#include "ThreadPool/WorkThreadPool.h"
#include "Logger/AsyncLogger.h"



WorkThreadPool::WorkThreadPool(int num) : num_(num > 0 ? num : 4) 
{
    for (int i = 0; i < num_; ++i)
    threads_.emplace_back([this] { worker(); });
}

WorkThreadPool::~WorkThreadPool() { stop(); }


void WorkThreadPool::submit(Task task)
{
    // 队列停止后，tryPush 返回 false。
    // 按接口约定，此时 task 尚未被移动，可以继续访问它的回调。
    if (!queue_.tryPush(std::move(task)))
    {
        LOG_WARN("WorkThreadPool: rejected task because the queue is stopped");

        // 任务没有执行，也必须通知 Connection 收尾，
        // 否则 taskRunning_ 可能一直保持 true，连接无法完成关闭。
        if (task.onComplete_)
        {
            try
            {
                task.onComplete_();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("WorkThreadPool: rejected task completion threw: "
                          + std::string(e.what()));
            }
            catch (...)
            {
                LOG_ERROR("WorkThreadPool: rejected task completion threw unknown exception");
            }
        }
    }
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

        // 关键：包 try-catch，防止业务抛异常把工作线程弄死
        try 
        {
            task.run_();
        } 
        catch (const std::exception& e) 
        {
            LOG_ERROR("WorkThreadPool: task.run_ threw: " + std::string(e.what()));
        } 
        catch (...) 
        {
            LOG_ERROR("WorkThreadPool: task.run_ threw unknown exception");
        }    

        // onComplete_ 也要保护
        // 否则 FIFO 链条会断，后续 task 永远不执行
        if (task.onComplete_) 
        {
            try 
            {
                task.onComplete_();
            } 
            catch (const std::exception& e) 
            {
                LOG_ERROR("WorkThreadPool: task.onComplete_ threw: " + std::string(e.what()));
            } 
            catch (...) 
            {
                LOG_ERROR("WorkThreadPool: task.onComplete_ threw unknown exception");
            }
        }
    }
}

void WorkThreadPool::stop()
{
    bool expected = false;
    if (!stop_.compare_exchange_strong (expected, true)) return;

    queue_.stop();  // 唤醒所有等待线程
    
    for (auto& t : threads_) if (t.joinable()) t.join();
}