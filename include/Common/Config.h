#pragma once

#include <string>
#include <cstdint>

// 全局配置单例，支持从命令行读取参数，避免硬编码端口和线程数
struct Config {
    uint16_t port = 8080;        // 监听端口，默认 8080
    int io_threads = 4;          // I/O 线程池大小（子 Reactor 数量）
    int work_threads = 4;        // 业务线程池大小

    // 获取单例
    static Config& instance();

    // 解析命令行参数，支持 --port, --io, --work, --help
    void parseArgs(int argc, char* argv[]);
};