#pragma once
#include <memory>
#include <any>
#include <functional>


class Connection;
// 任务结构体，在工作线程池中传递
struct Task 
{
    std::shared_ptr<Connection> conn_;   // 保证Connection存活
    std::any message_;                   // 解析后的请求（如HttpRequest）
    std::function<void()> run_;          // 为了让work不知道业务真正的解耦
};