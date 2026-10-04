#pragma once
#include <memory>
#include <any>
#include <functional>


class Connection;
// 任务结构体，在工作线程池中传递
struct Task 
{
    std::shared_ptr<Connection> conn_;   // 保证 Connection 存活
    std::any message_;                   // 解析后的请求消息
    std::function<void()> run_;          // 真正执行的业务逻辑

    // 当前任务执行完成后，通知 Connection：
    // “我执行完了，可以继续执行下一个任务了”
    std::function<void()> onComplete_;
};