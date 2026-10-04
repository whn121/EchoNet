#pragma once

#include "RateLimit/RateLimiter.h"
#include <chrono>
#include <unordered_map>
#include <mutex>



class TokenBucket : public RateLimiter
{
public:
    TokenBucket(double rate = 100.0, double capacity = 200.0);

    bool tryAcquire(const std::string& key) override;

    void removeKey(const std::string& key) override;

private:
    struct Bucket
    {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };
    
    void refill(Bucket& b);

    double rate_; // 令牌生成速率
    double capacity_; // 桶容量

    std::unordered_map<std::string, Bucket> buckets_;
    std::mutex mutex_;

};