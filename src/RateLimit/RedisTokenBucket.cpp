#include "RateLimit/RedisTokenBucket.h"
#include "Logger/AsyncLogger.h"
#include <chrono>


// Lua 脚本：读取-计算-写回，Redis 单线程执行保证原子性
// KEYS[1] = 令牌桶 key
// ARGV[1] = rate（令牌生成速率，个/秒）
// ARGV[2] = capacity（桶容量）
// ARGV[3] = now（当前时间戳，秒）
static const char* LUA_SCRIPT = R"(
local key = KEYS[1]
local rate = tonumber(ARGV[1])
local capacity = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

-- 读取当前令牌数（不存在则视为满桶）
local tokens = tonumber(redis.call('GET', key) or capacity)
-- 读取上次补充时间（不存在则视为当前时间）
local last = tonumber(redis.call('GET', key .. ':t') or now)

-- 按时间差补充令牌
local elapsed = now - last
tokens = math.min(capacity, tokens + elapsed * rate)

-- 判断是否够一个令牌
local allowed = 0
if tokens >= 1 then
    tokens = tokens - 1
    allowed = 1
end

-- 写回并设置过期（避免冷 key 占内存）
redis.call('SET', key, tokens)
redis.call('SET', key .. ':t', now)
redis.call('EXPIRE', key, 60)
redis.call('EXPIRE', key .. ':t', 60)

return allowed
)";


RedisTokenBucket::RedisTokenBucket(std::shared_ptr<RedisPool> pool, double rate, double capacity)
                                : pool_(std::move(pool)), rate_(rate), capacity_(capacity) {}

bool RedisTokenBucket::tryAcquire(const std::string& key) 
{
    // 1. 从连接池借连接
    auto conn = pool_->getConnection();
    if (!conn) 
    {
        // 降级：Redis 挂了，放行。限流器不是核心功能，不能成为单点故障
        LOG_WARN("Redis unavailable, rate limit bypassed");
        return true;
    }

    // 2. 当前时间戳（秒）
    long now = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count();

    // 3. 执行 Lua 脚本
    //    EVAL script numkeys key arg1 arg2 arg3
    redisReply* reply = (redisReply*)redisCommand (conn.get(), "EVAL %s 1 %s %f %f %ld", LUA_SCRIPT, key.c_str(), rate_, capacity_, now);

    // 4. 解析结果：1 = 通过，0 = 超限
    bool allowed = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) 
    {
        allowed = (reply->integer == 1);
    } 
    else if (reply) 
    {
        LOG_ERROR("Redis rate limit script returned unexpected type");
    }

    // 5. 释放回复对象
    if (reply) freeReplyObject(reply);
    return allowed;

}

void RedisTokenBucket::removeKey(const std::string& key) {
    auto conn = pool_->getConnection();
    if (!conn) {
        LOG_WARN("Redis unavailable, skip removeKey");
        return;
    }
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "DEL %s", key.c_str());
    if (reply) freeReplyObject(reply);
}