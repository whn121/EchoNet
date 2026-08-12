#pragma once

#include "HTTP/HTTP.h"
#include <mutex>

// 业务逻辑示例：简单的内存存储
// GET 返回当前存储的内容，POST 将 body 追加到存储中
class HttpService {
public:
    static HttpResponse handle(const HttpRequest& httprequest);  // 静态方法，无需实例

private:
    static std::string memory_;    // 模拟持久化存储（所有线程共享）
    static std::mutex mtx_;        // 保护 memory_ 的互斥锁
};