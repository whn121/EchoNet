#include "Common/Config.h"
#include <cstdlib>
#include <getopt.h>     // GNU 长选项解析
#include <iostream>

Config& Config::instance() {
    static Config cfg;   // C++11 保证线程安全的局部静态变量
    return cfg;
}

void Config::parseArgs(int argc, char* argv[]) {
    // 定义长选项
    static struct option long_options[] = {
        {"port",     required_argument, 0, 'p'},
        {"io",       required_argument, 0, 'i'},
        {"work",     required_argument, 0, 'w'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}                     // 结束标记
    };

    int opt;
    // 循环解析参数，直到返回 -1
    while ((opt = getopt_long(argc, argv, "p:i:w:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'p': port = std::atoi(optarg); break;   // 端口
            case 'i': io_threads = std::atoi(optarg); break; // I/O 线程数
            case 'w': work_threads = std::atoi(optarg); break;// 工作线程数
            case 'h':
                std::cout << "Usage: server [--port=8080] [--io=4] [--work=4]\n";
                exit(0);
        }
    }
}