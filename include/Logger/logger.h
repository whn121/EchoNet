#pragma once

#include <string>
#include <iostream>
#include <cerrno> //全局错误是一个全局整形变量
#include <cstring>

#include <chrono>
#include <mutex>
#include <sstream>
#include <iomanip>     // put_time, setw, setfill


class logger
{
public:
    //日志级别
    enum LEVEL //枚举
    {
        INFO = 0,
        WARN = 1,
        ERROR = 2
    };

    //获得单例,保证整个程序只有一个对象实例,全局获取
    static logger& instance()
    {
        static logger logger_; //保证线程安全
        return logger_;
    }

    //日志打印, 要确保线程安全
    void Log(LEVEL level, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock (mutex_);
        const char* levelStr[] = {"INFO", "WARN", "ERROR"};
        std::cout << " [ " << currentTime() << " ] " << " [ " << levelStr[level] << " ] " << msg <<std::endl;
    }
    
private:
    std::mutex mutex_;

    //获得当时时间戳字符串,格式：2024-08-12 14:35:22.123
    static std::string currentTime()
    {
        auto now = std::chrono::system_clock::now (); //获得当前时间
        auto time_t_now = std::chrono::system_clock::to_time_t (now); //转换为time_t类型
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch()) % 1000; //取毫秒部分 时间单位转化函数duration_cast
        
        // 先保存 localtime 结果，避免临时对象被覆盖
        struct tm local_tm;
        localtime_r(&time_t_now, &local_tm);   // 线程安全版本 

        std::stringstream ss;
        ss << std::put_time (std::localtime (&time_t_now), "%Y-%m-%d %H:%M:%S"); //格式化年月日时分秒
        ss << "." << std::setfill ('0') << std::setw (3) << ms.count(); //追加毫秒 固定3位填充0

        return ss.str();
    }

};

// 便捷宏，LOG_ERROR 自动附带 errno 信息
#define INFO(msg) logger::instance().Log(logger::LEVEL::INFO, msg)
#define WARN(msg) logger::instance().Log(logger::LEVEL::WARN, msg)
#define ERROR(msg) logger::instance().Log(logger::LEVEL::ERROR, msg)
