#pragma once

#include <any>
#include <string>
#include <optional>

class Buffer;   // 前向声明

// 解析结果枚举，让上层（Connection）能够区分三种状态
enum class ParseResult {
    OK,        // 成功解析出一个完整消息
    NEED_MORE, // 数据不足，需要继续接收
    ERROR      // 协议错误，应返回错误响应或关闭连接
};

// 协议抽象基类，所有具体协议（HTTP、自定义协议等）必须继承它
class Protocol {
public:
    virtual ~Protocol() = default;

    // 从 buffer 中解析数据
    virtual ParseResult parse(Buffer& buffer) = 0;

    // 将业务层的响应消息（any 包装）编码为网络发送的字节流
    virtual std::string encode(const std::any& message) = 0;

    // 获取解析后的请求消息（any 包装，实际类型由协议决定）
    virtual std::any getMessage() = 0;

    // 重置内部状态，准备解析下一个消息
    virtual void reset() = 0;

    // 获取协议层构建的错误响应（例如 HTTP 400）
    // 返回 optional<any>，如果没有错误则返回 nullopt
    virtual std::optional<std::any> getErrorResponse() {
        return std::nullopt;
    }
};