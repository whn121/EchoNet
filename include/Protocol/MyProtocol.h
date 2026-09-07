#pragma once

#include "Protocol/Protocol.h"
#include <string>
#include <any>
#include "Net/Buffer.h"
#include <netinet/in.h> //二进制转换


//我的协议格式| 4字节长度 | 2字节类型 | 4字节请求ID | 变长payload |

enum class MyType : u_int16_t
{
    //登录
    LOGIN_REQ    = 0x01,   // 客户端 -> 服务器，携带 username|password
    LOGIN_RESP   = 0x02,   // 服务器 -> 客户端，返回登录结果
    //创建room
    CREATE_ROOM_REQ  = 0x03,  // 客户端 -> 服务器，携带 room_name
    CREATE_ROOM_RESP = 0x04,  // 服务器 -> 客户端，返回 room_id
    //加入room
    JOIN_ROOM_REQ    = 0x05,  // 客户端 -> 服务器，携带 room_id
    JOIN_ROOM_RESP   = 0x06,  // 服务器 -> 客户端，返回加入结果
    //离开room
    LEAVE_ROOM_REQ   = 0x07,  // 客户端 -> 服务器，携带 room_id
    LEAVE_ROOM_RESP  = 0x08,  // 服务器 -> 客户端，返回离开结果
    //发送消息
    SEND_MSG_REQ    = 0x09,  // 客户端 -> 服务器，携带 room_id|content
    SEND_MSG_RESP   = 0x0A,  // 服务器 -> 发送者，确认发送成功
    //广播发给all
    BROADCAST_MSG   = 0x0B,  // 服务器 -> 房间内所有成员（包括发送者），携带 room_id|username|content
    //心跳(心跳就是客户端和服务端之间，每隔一段时间互相发一条极小的探测数据包，证明对方还活着、连接没断)
    HEARTBEAT_REQ   = 0x0C,  // 客户端 -> 服务器，无 payload 或带时间戳
    HEARTBEAT_RESP  = 0x0D,  // 服务器 -> 客户端，无 payload 或带时间戳
    //错误处理
    ERROR_RESP      = 0x0E  // 服务器 -> 客户端，携带错误码和错误消息

};

struct MyMessage
{
    MyType type_;
    u_int32_t id_;
    std::string payload_; 
};

class MyProtocol : public Protocol
{
public:
    ParseResult parse (Buffer& bufffer) override; //解析
    std::string encode (const std::any& message) override; //序列化二进制
    std::any getMessage() override; //获得最终消息
    void reset() override; //重置状态

    std::optional<std::any> getErrorResponse() override; //错误处理

    //满足虚继承参数不变
    // 辅助编码函数:根据类型和payload生成完整二进制消息
    static std::string encodeMessage (MyType type, uint32_t id, const std::string& payload);

private:
    MyMessage currentMessage_; // 最新消息
    bool hasError_ = false;
    std::string errorPayload_;

};