#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <Net/Buffer.h>  // 原路径，实际可能需调整

// HTTP方法枚举
enum class Method { GET, POST, UNKNOWN };
// HTTP版本枚举
enum class Version { HTTP10, HTTP11, UNKNOWN };

// HTTP请求结构体
struct HttpRequest {
    Method method_ = Method::UNKNOWN;
    std::string path_;
    Version version_ = Version::UNKNOWN;
    std::unordered_map<std::string, std::string> header_;
    std::string body_;
};

// HTTP响应结构体
struct HttpResponse {
    Version version_ = Version::UNKNOWN;
    int status_code_;
    std::string status_msg_;
    std::unordered_map<std::string, std::string> header_;
    std::string body_;
};