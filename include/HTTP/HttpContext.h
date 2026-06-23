#pragma once
#include "HTTP.h"

struct HttpContext
{
    enum class ParseState 
    {
        REQUEST_LINE,
        HEADERS,
        BODY,
        DONE
    };

    ParseState parsestate_ = ParseState::REQUEST_LINE;

    bool lineContext(const std::string& var, HttpRequest& httprequest);
    bool headerContext(const std::string& var, HttpRequest& httprequest);
    bool bodyContext(const std::string& var, HttpRequest& httprequest);
    size_t parse(const char* data, size_t len, HttpRequest& httprequest, bool& ifok);

};