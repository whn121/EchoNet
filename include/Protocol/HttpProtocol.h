#pragma once

#include "Protocol.h"
#include "HTTP/HttpContext.h"
#include "HTTP/HTTP.h"
#include <optional> //安全表示一个值的有无,代替裸指针空值,标记值


// HTTP 协议实现，继承 Protocol 抽象类
class HttpProtocol : public Protocol {
public:
    ~HttpProtocol() override;

    ParseResult parse(Buffer& buffer) override;               // 解析网络数据
    void reset() override;                                    // 重置状态
    std::string encode(const std::any& message) override;     // 编码响应
    std::any getMessage() override;                           // 获取解析后的请求
    std::optional<std::any> getErrorResponse() override;      // 获取错误响应

private:
    HttpContext httpcontext_;          // HTTP 解析器状态机
    HttpRequest httprequest_;          // 解析结果
    HttpResponse errorResponse_;       // 解析失败时构建的 400 响应
    bool hasError_ = false;            // 标记是否有错误发生
};