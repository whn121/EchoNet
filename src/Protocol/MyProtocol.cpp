#include "Protocol/MyProtocol.h"
#include <netinet/in.h>


//| 4字节长度(网络字节序) | 2字节类型 | 4字节请求ID | 变长payload |

ParseResult MyProtocol::parse (Buffer& buffer)
{
    const char* data = buffer.peek();
    size_t len = buffer.getreadable();

    if (len < 4)
    {
        return ParseResult::NEED_MORE;
    }

    uint32_t net_body_len;
    memcpy (&net_body_len, data, 4);//原样一次字节拷贝,按第一个参数类型规则解读
    uint32_t body_len = ntohl (net_body_len);

    //边界检测防止传一个小的数
    if (body_len < 6) 
    {
        hasError_ = true;
        shouldSendError_ = false;   // 致命错误，直接关连接
        errorPayload_ = "Invalid body length";
        return ParseResult::ERROR;
    }

    //上限检查防止永远无法满足len >= 4 + body_len一直报错
    const uint32_t MAX_BODY_LEN = 1024 * 1024;   // 1MB
    if (body_len > MAX_BODY_LEN) 
    {
        hasError_ = true;
        shouldSendError_ = false;   // 不发送错误响应
        errorPayload_ = "Message too large";
        return ParseResult::ERROR;
    }

    if (len < (4 + body_len)) return ParseResult::NEED_MORE;
    data += 4;

    uint16_t net_type;
    memcpy (&net_type, data, 2);
    uint16_t type_val = ntohs (net_type);
    currentMessage_.type_ = MyType(type_val);

    uint32_t net_id;
    memcpy (&net_id, data + 2, 4);
    uint32_t id = ntohl (net_id);
    currentMessage_.id_ = id;

    size_t pay_len = body_len - 6;
    currentMessage_.payload_.assign (data + 6, pay_len);

    buffer.goReadPtr (4 + body_len);

    //类型检测
    if (!isValidMsgType(type_val))
    {
        hasError_ = true;
        shouldSendError_ = true;
        errorPayload_ = "Unknown message type";
        return ParseResult::ERROR;
    }

    hasError_ = false;
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
    hasError_ = false;
    shouldSendError_ = false;    // 新增
    currentMessage_ = MyMessage();
    errorPayload_.clear();
}

std::optional<std::any> MyProtocol::getErrorResponse()
{
    if (hasError_ && shouldSendError_)
    {
        MyMessage errMsg;
        errMsg.type_ = MyType::ERROR_RESP;
        errMsg.id_ = 0;
        errMsg.payload_ = errorPayload_;
        return errMsg;
    }
    return std::nullopt; //shouldSendError_ = false;   // 不发送错误响应
}

