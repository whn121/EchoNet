#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <Net/Buffer.h>

enum class Method
{
    GET,
    POST,
    UNKNOWN
};

enum class Version
{
    HTTP10,
    HTTP11,
    UNKNOWN
};

struct HttpRequest
{
    Method method_ = Method::UNKNOWN;
    std::string path_; 
    Version version_ = Version::UNKNOWN;
    std::unordered_map<std::string ,std::string> header_;
    std::string body_;

};

struct HttpResponse
{
    Version version_ = Version::UNKNOWN;
    int status_code_;
    std::string status_msg_;
    std::unordered_map<std::string ,std::string> header_;
    std::string body_;

};