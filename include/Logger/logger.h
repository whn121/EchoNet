#pragma once

#include <iostream>
#include <cerrno>
#include <cstring>

class logger
{
public:
    enum class LEVEL //枚举
    {
        INFO = 1,
        ERROR = 2
    };
    template <typename T>
    static void log (LEVEL level, const T& msg);
};

template <typename T>
void logger::log(LEVEL level, const T &msg)
{
    switch(level)
    {
        case LEVEL::INFO : 
        std::cout << "[INFO]" << msg << std::endl;
        break;
        case LEVEL::ERROR :
        std::cout << "[ERROR]" << msg << " " << strerror(errno) << std::endl;
        break;
    }
}

#define INFO(x) logger::log(logger::LEVEL::INFO, x) //静态成员函数
#define ERROR(x) logger::log(logger::LEVEL::ERROR, x) //静态成员函数