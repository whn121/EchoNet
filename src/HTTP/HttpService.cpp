#include "HTTP/HttpService.h"
#include "Logger/logger.h"


std::string HttpService::memory_;
std::mutex HttpService::mtx_;

HttpResponse HttpService::handle(const HttpRequest& httprequest)
{
    HttpResponse httpresponse;
    std::lock_guard<std::mutex> lock (mtx_);
    if (httprequest.method_ == Method::GET)
    {
        httpresponse.body_ = memory_ + "\n";
    }
    if (httprequest.method_ == Method::POST)
    {
        httpresponse.body_ = "我收到并存在memory里\n";
        memory_.append (httprequest.body_);
    }
    httpresponse.version_ = httprequest.version_;
    httpresponse.status_code_ = 200;
    httpresponse.status_msg_ = "ok";
   
    httpresponse.header_["Server"] = "MyServer/1.0";
    httpresponse.header_["Content-Length"] = std::to_string(httpresponse.body_.size());
    httpresponse.header_["Connection"] = "keep-alive";

    INFO ("响应体生成完成");
    return httpresponse;
}