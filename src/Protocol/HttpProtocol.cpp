#include "Protocol/HttpProtocol.h"
#include "Net/Buffer.h"
#include <sstream>

HttpProtocol::~HttpProtocol() {}

ParseResult HttpProtocol::parse(Buffer& buffer) {
    // 委托给状态机解析
    ParseResult res = httpcontext_.parse(buffer, httprequest_);
    if (res == ParseResult::ERROR) {
        // 构造一个 400 Bad Request 响应，供 Connection 层取用
        errorResponse_.version_ = Version::HTTP11;
        errorResponse_.status_code_ = 400;
        errorResponse_.status_msg_ = "Bad Request";
        errorResponse_.header_["Content-Length"] = "0";
        errorResponse_.header_["Connection"] = "keep-alive";
        errorResponse_.body_ = "";
        hasError_ = true;
    }
    return res;
}

void HttpProtocol::reset() {
    httprequest_ = HttpRequest();                 // 请求对象清零
    httpcontext_.parsestate_ = HttpContext::ParseState::REQUEST_LINE; // 状态重置
    httpcontext_.isError_ = false;
    hasError_ = false;
}

std::any HttpProtocol::getMessage() {
    return httprequest_;   // 返回解析好的 HttpRequest
}

std::string HttpProtocol::encode(const std::any& message) {
    // 从 any 中取出 HttpResponse
    auto response = std::any_cast<HttpResponse>(message);
    std::stringstream ss;

    // 状态行 HTTP/1.1 200 OK
    std::string ver = (response.version_ == Version::HTTP11) ? "HTTP/1.1" : "HTTP/1.0";
    ss << ver << " " << response.status_code_ << " " << response.status_msg_ << "\r\n";

    // 头部字段
    for (const auto& [key, value] : response.header_) {
        ss << key << ": " << value << "\r\n";
    }

    ss << "\r\n" << response.body_;   // 空行 + 消息体
    return ss.str();
}

std::optional<std::any> HttpProtocol::getErrorResponse() {
    if (hasError_) return errorResponse_;   // 返回错误响应
    return std::nullopt;                    // 无错误
}