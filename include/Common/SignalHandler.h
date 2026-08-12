#pragma once

//优雅推出组件

#include <csignal> //Linux信号库 signal(),SIGINT等
#include <functional> //装各种调用对象(函数)

// 信号处理器，用于优雅退出。
// 不依赖全局变量，通过回调函数解耦。
class SignalHandler {
public:
    // 初始化，注册 SIGINT 和 SIGTERM 的处理函数
    // onStop: 当信号到达时调用的回调，通常用于停止主事件循环
    static void init(std::function<void()> onStop) {
        stopCallback_ = onStop;               // 保存用户回调
        signal(SIGINT, handler);              // Ctrl+C
        signal(SIGTERM, handler);             // kill 命令
    }

private:
    // 实际的信号处理函数（必须是静态的）
    static void handler(int) {
        if (stopCallback_) stopCallback_();   // 执行用户注册的停止逻辑
    }

    static std::function<void()> stopCallback_; // 存储的回调函数
};

// 静态成员定义（放在头文件里，采用 C++17 inline 可避免多重定义，这里用传统写法）
std::function<void()> SignalHandler::stopCallback_;