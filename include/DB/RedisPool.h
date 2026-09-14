#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>


class RedisPool
{
public:
    RedisPool(const std::string& host, int port, int pool_size = 4);
    ~RedisPool();

    std::shared_ptr<redisContext> getConnection();
    void releaseConnection(redisContext* conn);

private:
    std::queue<redisContext*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string host_;
    int port_;
    int pool_size_;

};