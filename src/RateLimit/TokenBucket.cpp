#include "RateLimit/TokenBucket.h"
#include <algorithm>


TokenBucket::TokenBucket(double rate, double capacity) : rate_ (rate), capacity_ (capacity) {}

void TokenBucket::refill (Bucket& b)
{
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double> (now - b.last_refill).count();
    b.tokens = std::min(capacity_, b.tokens + elapsed * rate_);
    b.last_refill = now;
}

bool TokenBucket::tryAcquire(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = buckets_.find(key);
    if (it == buckets_.end())
    {
        Bucket b;
        b.tokens = capacity_;
        b.last_refill = std::chrono::steady_clock::now();
        it = buckets_.emplace(key, b).first;
    }

    refill(it->second);

    if (it->second.tokens >= 1.0) 
    {
        it->second.tokens -= 1.0;
        return true;
    }
    return false;

}

void TokenBucket::removeKey(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_.erase(key);
}