#pragma once
#include "HttpContext.h"
#include <mutex>

class HttpService
{
private:
    static std::string memory_;
    static std::mutex mtx_;

public:
    HttpResponse handle(const HttpRequest& httprequest);

};