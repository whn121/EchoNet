// 定义"后端节点" 结构体
#pragma once

#include <string>
#include <cstdint>


struct ServiceInstance
{
    std::string service_name;  // 服务名
    std::string instance_id;   // 实例id
    std::string host;          // IP
    uint16_t port;             // 端口

    std::string address() const
    {
        return host + ":" + std::to_string(port);
    }
};

