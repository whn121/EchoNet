#pragma once

#include <string>

class RateLimiter {
public:
    virtual ~RateLimiter() = default;

    // 尝试获取一个令牌
    // key: 限流维度，如 "global" / "user:100" / "fd:3"
    // 返回 true = 通过，false = 超限
    virtual bool tryAcquire(const std::string& key) = 0;

    // 清理某个 key 的状态，防止内存/Redis key 无限增长
    virtual void removeKey(const std::string& key) = 0;
};