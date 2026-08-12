#pragma once

#include "Protocol/Protocol.h"   // 需要 ParseResult 枚举
#include "HTTP.h"                // HttpRequest, Method, Version 等
#include "Net/Buffer.h"          // Buffer 类

// HTTP 请求解析器（状态机）
// 每次从 Buffer 中读取数据，逐步解析一个完整的 HTTP 请求
// 支持长连接：一个连接可以连续解析多个请求（通过 reset() 重置状态）
struct HttpContext
{
    // 解析状态枚举：状态机共有 4 个状态
    enum class ParseState 
    {
        REQUEST_LINE,  // 正在解析请求行（GET /index.html HTTP/1.1）
        HEADERS,       // 正在解析头部字段（Key: Value）
        BODY,          // 正在解析消息体（根据 Content-Length 读取）
        DONE           // 一个完整的请求已解析完成
    };

    ParseState parsestate_ = ParseState::REQUEST_LINE;  // 当前解析状态，初始为请求行
    bool isError_ = false;                              // 是否发生解析错误（供外部查询）

    // 解析请求行，例如 "GET / HTTP/1.1"
    bool lineContext(const std::string& line, HttpRequest& httprequest);

    // 解析单个头部行，例如 "Host: localhost"
    bool headerContext(const std::string& header, HttpRequest& httprequest);

    // 解析消息体（纯文本，直接赋值）
    bool bodyContext(const std::string& body, HttpRequest& httprequest);

    // 主解析函数：从 Buffer 中读取数据，推进状态机
    // 返回值：
    //   OK         —— 成功解析出一个完整的请求
    //   NEED_MORE  —— 数据不足，需要继续接收
    //   ERROR      —— 协议错误，应该返回错误响应或关闭连接
    ParseResult parse(Buffer& buffer, HttpRequest& httprequest);

    // 查询是否发生解析错误
    bool isError() const { return isError_; }
};