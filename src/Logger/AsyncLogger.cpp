#include "Logger/AsyncLogger.h"


AsyncLogger::AsyncLogger() 
{
    file_.open("echonet.log", std::ios::app);
    write_thread_ = std::thread([this] { writeLoop(); });
}

AsyncLogger::~AsyncLogger() {
    stop();
}

AsyncLogger& AsyncLogger::instance() 
{
    static AsyncLogger logger;
    return logger;
}

void AsyncLogger::log(Level level, const std::string& msg) 
{
    const char* levelStr[] = {"INFO", "WARN", "ERROR"};
    std::string line = "[" + currentTime() + "] [" + levelStr[level] + "] " + msg + "\n";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(line));
    }
    cv_.notify_one();
}

void AsyncLogger::writeLoop() 
{
    while (true) 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });

        if (stop_ && queue_.empty()) break;

        // 批量取出一批
        std::queue<std::string> local;
        local.swap(queue_);       // 交换，O(1)
        lock.unlock();            // 解锁后再写文件

        while (!local.empty()) 
        {
            file_ << local.front();
            local.pop();
        }
        file_.flush();
    }
}

void AsyncLogger::stop() 
{
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) return;
    cv_.notify_one();
    if (write_thread_.joinable()) write_thread_.join();
}

std::string AsyncLogger::currentTime() 
{
    auto now = std::chrono::system_clock::now (); //获得当前时间
        auto time_t_now = std::chrono::system_clock::to_time_t (now); //转换为time_t类型
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch()) % 1000; //取毫秒部分 时间单位转化函数duration_cast
        
        // 先保存 localtime 结果，避免临时对象被覆盖
        struct tm local_tm;
        localtime_r(&time_t_now, &local_tm);   // 线程安全版本 

        std::stringstream ss;
        ss << std::put_time (&local_tm, "%Y-%m-%d %H:%M:%S"); //格式化年月日时分秒
        ss << "." << std::setfill ('0') << std::setw (3) << ms.count(); //追加毫秒 固定3位填充0

        return ss.str();
}