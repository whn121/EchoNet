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
    if (type_val < uint16_t (MyType::LOGIN_REQ) || type_val > uint16_t (MyType::ERROR_RESP))
    {
        hasError_ = true;
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
    currentMessage_ = MyMessage();
    errorPayload_.clear();
}

std::optional<std::any> MyProtocol::getErrorResponse()
{
    if (hasError_)
    {
        MyMessage errMsg;
        errMsg.type_ = MyType::ERROR_RESP;
        errMsg.id_ = 0;
        errMsg.payload_ = errorPayload_;
        return errMsg;
    }
    return std::nullopt;
}

