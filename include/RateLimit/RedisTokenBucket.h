#pragma once
#include "RateLimit/RateLimiter.h"
#include "DB/RedisPool.h"
#include <memory>


class RedisTokenBucket : public RateLimiter
{
public:
    RedisTokenBucket(std::shared_ptr<RedisPool> pool, double rate = 100.0, double capacity = 200.0);

    bool tryAcquire(const std::string& key) override;

    void removeKey(const std::string& key) override;

private:
    std::shared_ptr<RedisPool> pool_; //不需要锁因为redis单线程执行lua天然原子
    double rate_;
    double capacity_;

};