#include "HTTP/HttpService.h"
#include "Logger/logger.h"

std::string HttpService::memory_;
std::mutex HttpService::mtx_;

HttpResponse HttpService::handle(const HttpRequest& httprequest) {
    HttpResponse httpresponse;
    std::lock_guard<std::mutex> lock(mtx_);   // 锁住，保证线程安全

    if (httprequest.method_ == Method::GET) {
        // GET：返回 memory_ 中的内容，末尾加换行
        httpresponse.body_ = memory_ + "\n";
    } else if (httprequest.method_ == Method::POST) {
        // POST：追加 body 到 memory_
        httpresponse.body_ = "我收到并存在memory里\n";
        memory_.append(httprequest.body_);
    }

    // 填充响应基本信息
    httpresponse.version_ = httprequest.version_;
    httpresponse.status_code_ = 200;
    httpresponse.status_msg_ = "OK";
    httpresponse.header_["Server"] = "MyServer/1.0";
    httpresponse.header_["Content-Length"] = std::to_string(httpresponse.body_.size());
    httpresponse.header_["Connection"] = "keep-alive";   // 支持长连接

    INFO("响应体生成完成");
    return httpresponse;
}