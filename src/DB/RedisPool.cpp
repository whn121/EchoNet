#include "DB/RedisPool.h"
#include "Logger/AsyncLogger.h"

RedisPool::RedisPool(const std::string& host, int port, int pool_size) : host_ (host), port_ (port), pool_size_ (pool_size)
{
    for (int i = 0; i < pool_size_; i++)
    {
        redisContext* conn = redisConnect (host_.c_str(), port_);
        if (conn == nullptr || conn -> err)
        {
            if (conn)
            {
                LOG_ERROR("Redis connect failed: " + std::string(conn->errstr));
                redisFree(conn); //关闭链接
            }
            continue;
        }
        pool_.emplace(conn);
    }
}

RedisPool::~RedisPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        redisContext* conn = pool_.front();
        pool_.pop();
        redisFree(conn);
    }
}

std::shared_ptr<redisContext> RedisPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);

    bool ok = cv_.wait_for(lock, std::chrono::milliseconds(500),
                            [this] { return !pool_.empty(); });

    if (!ok || pool_.empty()) {
        LOG_ERROR("RedisPool getConnection timeout");
        return nullptr;
    }

    redisContext* conn = pool_.front();
    pool_.pop();
    return std::shared_ptr<redisContext>(conn, [this](redisContext* c) {
        releaseConnection(c);
    });
}

void RedisPool::releaseConnection(redisContext* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}