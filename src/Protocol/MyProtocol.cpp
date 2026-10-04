#include "Protocol/MyProtocol.h"
#include <netinet/in.h>
#include "Logger/AsyncLogger.h"


//| 4字节长度(网络字节序) | 2字节类型 | 4字节请求ID | 变长payload |

ParseResult MyProtocol::parse(Buffer& buf)
{
    // 最小消息长度：4 + 2 + 4 = 10 字节
    static constexpr size_t MIN_MSG_SIZE = 10;
    static constexpr uint32_t MAX_BODY_SIZE = 16 * 1024 * 1024;  // 16MB

    // 第一步：读 4 字节 body_len
    if (buf.getreadable() < 4) 
    {
        return ParseResult::NEED_MORE;
    }

    uint32_t body_len = 0;
    std::memcpy(&body_len, buf.peek(), 4);
    body_len = ntohl(body_len);

    // 关键：body_len 上限检查，防止恶意超大包
    if (body_len > MAX_BODY_SIZE) 
    {
        LOG_ERROR("MyProtocol: body_len too large: " + std::to_string(body_len));
        // 协议错误：直接关连接，不返回错误响应
        return ParseResult::ERROR;
    }

    // body 至少要有 type(2) + id(4)
    if (body_len < 6) 
    {
        LOG_ERROR("MyProtocol: body_len too small: " + std::to_string(body_len));
        // 协议错误：直接关连接，不返回错误响应
        return ParseResult::ERROR;
    }

    // 第二步：判断完整包是否到达
    if (buf.getreadable() < 4 + body_len) 
    {
        return ParseResult::NEED_MORE;
    }

    // 第三步：完整包已到，开始解析
    buf.moveReadPtr(4);  // 跳过 body_len

    // 读 type(2)
    uint16_t type_raw = 0;
    std::memcpy(&type_raw, buf.peek(), 2);
    type_raw = ntohs(type_raw);
    buf.moveReadPtr(2);

    // 读 id(4)
    uint32_t id = 0;
    std::memcpy(&id, buf.peek(), 4);
    id = ntohl(id);
    buf.moveReadPtr(4);

    // 读 payload
    uint32_t payload_len = body_len - 6;
    std::string payload;
    if (payload_len > 0) 
    {
        payload.assign(buf.peek(), payload_len);
        buf.moveReadPtr(payload_len);
    }

    // 校验 type 合法性
    if (!isValidMsgType(type_raw)) 
    {
        LOG_ERROR("MyProtocol: invalid message type: " + std::to_string(type_raw));
        // 协议错误：直接关连接，不返回错误响应
        return ParseResult::ERROR;
    }

    MyType type = static_cast<MyType>(type_raw);

    // 填充消息
    currentMessage_.type_    = type;
    currentMessage_.id_      = id;
    currentMessage_.payload_ = std::move(payload);


    return ParseResult::OK;
}

std::string MyProtocol::encodeMessage(MyType type, uint32_t id, const std::string &payload)
{
    uint32_t body_len = 6 + payload.size();
    std::string packet;
    packet.reserve (4 + body_len);

    uint32_t net_body_len = htonl (body_len);
    packet.append ((reinterpret_cast<const char*> (&net_body_len)), 4);

    uint16_t vtype = uint16_t (type);
    uint16_t net_type = htons (vtype);
    packet.append ((reinterpret_cast<const char*> (&net_type)), 2);

    uint32_t net_id = htonl (id);
    packet.append ((reinterpret_cast<const char*> (&net_id)), 4);

    packet.append (payload.data(), payload.size());
    
    return packet;
}

bool  MyProtocol::isValidMsgType(uint16_t t)
{
    switch (static_cast<MyType>(t)) {
        case MyType::LOGIN_REQ:
        case MyType::LOGIN_RESP:
        case MyType::CREATE_ROOM_REQ:
        case MyType::CREATE_ROOM_RESP:
        case MyType::JOIN_ROOM_REQ:
        case MyType::JOIN_ROOM_RESP:
        case MyType::LEAVE_ROOM_REQ:
        case MyType::LEAVE_ROOM_RESP:
        case MyType::SEND_MSG_REQ:
        case MyType::SEND_MSG_RESP:
        case MyType::BROADCAST_MSG:
        case MyType::HEARTBEAT_REQ:
        case MyType::HEARTBEAT_RESP:
        case MyType::ERROR_RESP:
        case MyType::MEMBER_COUNT_UPDATE:
            return true;
        default:
            return false;
    }
}

std::string MyProtocol::encode(const std::any &message)
{
    auto msg = std::any_cast<MyMessage> (message);
    return encodeMessage(msg.type_, msg.id_, msg.payload_);
}

std::any MyProtocol::getMessage()
{
    return currentMessage_;
}

void MyProtocol::reset()
{
    currentMessage_ = MyMessage();
}

std::optional<std::any> MyProtocol::getErrorResponse()
{
    // 协议错误：直接关连接，不返回错误响应
    return std::nullopt;
}

