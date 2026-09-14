#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>


class AsyncLogger
{
public:
    enum Level {INFO, WARN, ERROR};

    static AsyncLogger& instance(); //单里入口

    void log (Level level,const std::string& msg); //业务线程调用，只入队
    void stop(); //停止日志线程并 flush

private:
    AsyncLogger();                    // 私有构造
    ~AsyncLogger();
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    void writeLoop();                 // 日志线程主循环
    std::string currentTime();        // 生成时间戳字符串

    std::queue<std::string> queue_;   // 日志队列
    std::mutex mutex_;                // 保护队列
    std::condition_variable cv_;      // 队列空时等待
    std::atomic<bool> stop_{false};   // 停止标志
    std::thread write_thread_;        // 日志线程
    std::ofstream file_;              // 输出文件

};


// 宏
#define LOG_INFO(msg)  AsyncLogger::instance().log(AsyncLogger::INFO, msg)
#define LOG_WARN(msg)  AsyncLogger::instance().log(AsyncLogger::WARN, msg)
#define LOG_ERROR(msg) AsyncLogger::instance().log(AsyncLogger::ERROR, msg)