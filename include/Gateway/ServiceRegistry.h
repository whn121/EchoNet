#pragma once

#include "ServiceRegistry/ServiceInstance.h"
#include "DB/RedisPool.h"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <string>


class ServiceRegistry
{
public:
    ServiceRegistry(std::shared_ptr<RedisPool> pool, const std::string& service_name);
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    void Start(int refresh_interval_sec = 2);
    void Stop();

    // 线程安全：返回当前快照
    std::vector<ServiceInstance> GetSnapshot() const;
    size_t Size() const;

private:
    void Refresh(); // 从redis把服务注册的信息读人内存
    void RefreshLoop(int interval_sec);

    std::shared_ptr<RedisPool> pool_;
    std::string service_name_;

    mutable std::mutex mutex_;
    std::vector<ServiceInstance> instances_;

    std::thread refresh_thread_;
    std::atomic<bool> stop_ {false};
};
