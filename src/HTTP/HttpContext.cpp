#include "HTTP/HttpContext.h"
#include "Logger/logger.h"

bool HttpContext::lineContext(const std::string& line, HttpRequest& httprequest)
{
    std::stringstream ss(line); //字符串流,以空格为间隔
    std::string method, path, version;
    ss >> method >> path >> version;
    if (method == "GET") httprequest.method_ = Method::GET;
    else if (method == "POST") httprequest.method_ = Method::POST;
    else httprequest.method_ = Method::UNKNOWN;
    httprequest.path_ = path;
    if (version == "HTTP/1.1") httprequest.version_ = Version::HTTP11;
    else if (version == "HTTP/1.0") httprequest.version_ = Version::HTTP10;
    else httprequest.version_ = Version::UNKNOWN;

    return true;
}

bool HttpContext::headerContext(const std::string& header, HttpRequest& httprequest)
{
    auto n = header.find(":");
    if (n == std::string::npos) return false;//数据不够
    std::string key = header.substr(0, n);
    std::string value = header.substr(n + 1);
    while(!value.empty() && value[0] == ' ')
    {
        value.erase(0, 1);
    }
    httprequest.header_[key] = value;

    return true;
}

bool HttpContext::bodyContext(const std::string& body, HttpRequest& httprequest)
{
    httprequest.body_ = body;

    return true; 
}

size_t HttpContext::parse(const char* data, size_t len, HttpRequest& httprequest, bool& ifok)
{
    if (parsestate_ == ParseState::DONE) 
    {
        ifok = true;
        parsestate_ = ParseState::REQUEST_LINE;
        return 0;  // 没有消费新数据
    }
    size_t consumed = 0; //记录消费的个数
    while(consumed < len)
    {
        if (parsestate_ == ParseState::DONE)
        {
            ifok = true;
            parsestate_ = ParseState::REQUEST_LINE;
            return consumed;
        }
        if (parsestate_ == ParseState::REQUEST_LINE)
        {
            const char* line_end = (const char*)memmem (data + consumed, len - consumed, "\r\n", 2);
            if (!line_end) break;
            size_t line_len = line_end - (data + consumed);
            bool iflc = lineContext(std::string(data + consumed, line_len), httprequest);
            if (!iflc) return 0;
            consumed += (line_len + 2);
            parsestate_ = ParseState::HEADERS;
            continue; //不加就是隐士推进循环,不便于优化
        }
        else if (parsestate_ == ParseState::HEADERS)
        {
            const char* headerline_end = (const char*)memmem (data + consumed, len - consumed, "\r\n", 2);
            if (!headerline_end) break;
            size_t headerline_len = headerline_end - (data + consumed);
            if (headerline_len == 0) 
            {
                consumed += 2;
                // 如果没有 Content-Length 头，说明没有请求体，解析直接完成
                if (httprequest.header_.find("Content-Length") == httprequest.header_.end()) 
                {
                    parsestate_ = ParseState::DONE;
                    ifok = true;
                } 
                else 
                {
                    parsestate_ = ParseState::BODY;
                }
                continue;
            }
            bool ifhc = headerContext (std::string (data + consumed, headerline_len), httprequest);
            if (!ifhc) return 0;
            consumed += (headerline_len + 2);

        }
        else if (parsestate_ == ParseState::BODY)
        {
            auto it = httprequest.header_.find ("Content-Length");
            if (it == httprequest.header_.end()) 
            {
                return 0;
            }
            size_t size_body = 0;
            try { size_body = std::stoul(it->second); }//stoi容易出问题这里不能简化
            catch (...) { return 0; } // 通知上层关闭连接...为所有异常
            if(size_body == 0) 
            {
                bool ifbc = bodyContext ("", httprequest);
                if (!ifbc) return 0;
                parsestate_ = ParseState::DONE;
                continue;
            }
            if (len - consumed < size_body) break;
            bool ifbc = bodyContext (std::string (data + consumed, size_body), httprequest);
            if (!ifbc) return 0;
            consumed += size_body;
            parsestate_ = ParseState::DONE;
            ifok = true;
            continue;
        }
    }
    INFO ("数据解析完成");
    return consumed;
}