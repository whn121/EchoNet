#include "Gateway/ServiceRegistry.h"
#include "Logger/AsyncLogger.h"
#include <chrono>


ServiceRegistry::ServiceRegistry(std::shared_ptr<RedisPool> pool, const std::string& service_name)
                                    :pool_ (std::move(pool)), service_name_ (service_name) {}


ServiceRegistry::~ServiceRegistry()
{
    Stop();
}

void ServiceRegistry::Start(int refresh_interval_sec)
{
    // 启动前先刷一次, 立即有数据
    Refresh();

    refresh_thread_ = std::thread([this, refresh_interval_sec]
    {
        RefreshLoop(refresh_interval_sec);
    });
}

void ServiceRegistry::Stop()
{
    stop_ = true;
    if (refresh_thread_.joinable()) refresh_thread_.join();
}

std::vector<ServiceInstance> ServiceRegistry::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return instances_; // 返回拷贝
}

 size_t ServiceRegistry::Size() const
 {
    std::lock_guard<std::mutex> lock(mutex_);
    return instances_.size();
 }

 void ServiceRegistry::RefreshLoop(int interval_sec)
 {
    while (!stop_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
        if (stop_) break;
        Refresh();
    }
 }

 void ServiceRegistry::Refresh()
 {
    auto conn = pool_->getConnection();
    if (!conn)
    {
        LOG_WARN("ServiceRegistry: Redis unavailable");
        return;
    }

    // 1.KEYS services:chat_service:*, 返回所有匹配的key没有value;
    std::string pattern = "services:" + service_name_ + ":*";
    redisReply* reply = (redisReply*)redisCommand (conn.get(), "KEYS %s", pattern.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) //REDIS_REPLY_ARRAY返回数组，数组里每一项又是独立 redisReply 对象。
    {
        if (reply) freeReplyObject(reply);
        return;
    } 

    std::vector<ServiceInstance> new_instances;
    new_instances.reserve (reply->elements); // 先定容量别忘了

    std::string prefix = "services:" + service_name_ + ":";

    for (size_t i = 0; i < reply->elements; ++i)
    {
        std::string key = reply->element[i]->str;
        std::string instance_id = key.substr(prefix.size());

        // 2.GET services:chat_service:node-9001  获得值
        redisReply* val = (redisReply*)redisCommand(conn.get(), "GET %s", key.c_str());

        if (val == nullptr || val->type != REDIS_REPLY_STRING) //REDIS_REPLY_STRING返回单个字符串
        {
            if (val) freeReplyObject(val);
            continue;
        }

        std::string addr = val->str;
        freeReplyObject(val);

        // 3.解析"host:port"
        auto pos = addr.find(':');
        if (pos == std::string::npos) continue;

        ServiceInstance inst;
        inst.service_name = service_name_;
        inst.instance_id = instance_id;
        inst.host = addr.substr(0, pos);
        inst.port = static_cast<uint16_t>(std::stoi(addr.substr(pos + 1)));

        new_instances.push_back(std::move(inst));
    }

    freeReplyObject(reply);

    // 4.更新本地缓存
    {
        std::lock_guard<std::mutex> lock(mutex_);
        instances_ = std::move(new_instances);
    }

    LOG_INFO("ServiceRegistry refreshed: " + std::to_string(instances_.size()) + " instances");

 }