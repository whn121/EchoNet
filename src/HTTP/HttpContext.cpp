#include "HTTP/HttpContext.h"
#include "Logger/logger.h"
#include <sstream>      // std::stringstream 用于拆分请求行
#include <cstring>      // memmem 函数（POSIX 扩展）
#include <string_view> //c++17新特性,只读字符串视图就好像(长度+内容)

// ---------- 解析请求行 ----------
bool HttpContext::lineContext(const std::string& line, HttpRequest& httprequest)
{
    std::stringstream ss(line);  // 用字符串流按空格分割
    std::string method, path, version;
    ss >> method >> path >> version;  // 提取三个部分

    // 识别 HTTP 方法
    if (method == "GET") httprequest.method_ = Method::GET;
    else if (method == "POST") httprequest.method_ = Method::POST;
    else httprequest.method_ = Method::UNKNOWN;

    httprequest.path_ = path;   // 保存路径

    // 识别 HTTP 版本
    if (version == "HTTP/1.1") httprequest.version_ = Version::HTTP11;
    else if (version == "HTTP/1.0") httprequest.version_ = Version::HTTP10;
    else httprequest.version_ = Version::UNKNOWN;

    // 如果方法未知（不是 GET/POST），视为非法请求
    if (httprequest.method_ == Method::UNKNOWN)
    {
        return false;   // 返回 false，上层将触发 ParseResult::ERROR
    }

    return true;
}

// ---------- 解析一个头部行 ----------
bool HttpContext::headerContext(const std::string& header, HttpRequest& httprequest)
{
    auto n = header.find(":");                // 查找冒号位置
    if (n == std::string::npos) return false; // 格式错误（没有冒号）

    std::string key = header.substr(0, n);     // 冒号前是键
    std::string value = header.substr(n + 1);  // 冒号后是值（可能包含前导空格）

    // 去除值前面的空格（HTTP 头部值可以包含前导空格）
    while(!value.empty() && value[0] == ' ')
    {
        value.erase(0, 1);
    }

    httprequest.header_[key] = value;   // 存入头部 map
    return true;
}

// ---------- 解析消息体 ----------
bool HttpContext::bodyContext(const std::string& body, HttpRequest& httprequest)
{
    httprequest.body_ = body;   // 直接将字节串赋给 body
    return true;
}

// ---------- 主解析状态机 ----------
ParseResult HttpContext::parse(Buffer& buffer, HttpRequest& httprequest)
{
    isError_ = false; //防止错误状态残留影响下一次解析

    const char* data = buffer.peek();   // 指向可读数据起始地址
    size_t len = buffer.getreadable();  // 可读数据长度

    if (len == 0)
    {
        return ParseResult::NEED_MORE;  // 没有任何数据，等待下次接收
    }

    size_t consumed = 0;  // 记录已消费（已解析）的字节数

    while(consumed < len)  // 不断处理，直到消费完所有数据或状态机需要更多数据
    {
        // ===== 状态：解析请求行 =====
        if (parsestate_ == ParseState::REQUEST_LINE)
        {
            // 在当前可读数据中查找 "\r\n"（行结束标记）memmem是拓展的不在标准库里所以不使用strng_view c++17新特性只读字符串视图fidn等方便不拷贝等
            std::string_view view(data + consumed, len - consumed);//获取视图只读几乎0开销,用于只读用完就扔
            auto pos = view.find("\r\n");
            if (pos == std::string_view::npos) break;

            size_t line_len = pos; //行长度(不包括\r\n)
            
            //调用解析lineContext
            if (!lineContext (std::string(data + consumed, line_len), httprequest)) 
            {
                isError_ = true;       //标记解析错误
                return ParseResult::ERROR; //返回错误
            } 

            consumed += line_len + 2;  // 跳过请求行和 \r\n
            parsestate_ = ParseState::HEADERS;  // 进入头部解析状态
            continue;  // 继续循环（因为可能后面紧跟着头部行）
        }
        // ===== 状态：解析头部 =====
        else if (parsestate_ == ParseState::HEADERS)
        {
            // 查找下一行的 \r\n
            std::string_view view(data + consumed, len - consumed);
            auto pos = view.find("\r\n");
            if (pos == std::string_view::npos) break;

            size_t headerline_len = pos;

            // 如果行长度为0，说明遇到了空行，头部结束
            if (headerline_len == 0) 
            {
                consumed += 2;  // 跳过空行（\r\n）

                // 判断是否有 Content-Length 头部
                if (httprequest.header_.find("Content-Length") == httprequest.header_.end()) 
                {
                    parsestate_ = ParseState::DONE;  // 没有 body，直接完成
                } 
                else 
                {
                    parsestate_ = ParseState::BODY;  // 进入 body 解析状态
                }
                continue;
            }

            // 解析这一行头部
            bool ifhc = headerContext(std::string(data + consumed, headerline_len), httprequest);
            if (!ifhc)
            {
                isError_ = true;
                return ParseResult::ERROR;
            }

            consumed += headerline_len + 2;  // 跳过这一行和 \r\n
        }
        // ===== 状态：解析消息体 =====
        else if (parsestate_ == ParseState::BODY)
        {
            auto it = httprequest.header_.find("Content-Length");
            if (it == httprequest.header_.end()) 
            {
                // 理论上这里不应该执行（进入 BODY 状态前已检查），但以防万一
                isError_ = true;
                return ParseResult::ERROR;
            }
            
            //进行"Content-Length"合法性检测
            const std::string& len_str = it -> second;

            //1.检测是否为纯数字且非空
            if (len_str.empty() || len_str.find_first_not_of("0123456789") != std::string::npos) 
            {
                LOG_ERROR("Invalid Content-Length: " + len_str);
                isError_ = true;
                return ParseResult::ERROR;
            }

            //2.转换为size_t, 捕获可能异常
            size_t size_body = 0;
            try 
            {
                size_body = std::stoul(len_str);  // 将字符串转为无符号长整型
            }
            catch (const std::exception& e) 
            {
                // 转换失败（如非数字），视为解析错误
                LOG_ERROR("Content-Length conversion failed: " + len_str);
                isError_ = true;
                return ParseResult::ERROR;
            }

            //3.限制最大body大小(例如1mb)
            const size_t MAX_HTTP_BODY_SIZE = 1024 * 1024;
            if (size_body > MAX_HTTP_BODY_SIZE)
            {
                LOG_ERROR("Content-Length too large: " + std::to_string(size_body));
                isError_ = true;
                return ParseResult::ERROR;
            }

            // 如果 body 长度为 0，直接完成
            if(size_body == 0) 
            {
                bodyContext("", httprequest);
                parsestate_ = ParseState::DONE;
                continue;
            }

            // 如果缓冲区中剩余数据不足 body 长度，等待更多数据
            if (len - consumed < size_body) break;

            // 读取 body
            bodyContext(std::string(data + consumed, size_body), httprequest);
            consumed += size_body;           // 消费 body 字节
            parsestate_ = ParseState::DONE;  // 解析完成
            continue;
        }
        // ===== 状态：解析完成 =====
        else if (parsestate_ == ParseState::DONE) 
        {
            break;  // 已解析完一个完整请求，退出循环
        }
    }

    // 移动 Buffer 的读指针，丢弃已解析的数据（释放缓冲区空间）
    if (consumed > 0)
    {
        buffer.goReadPtr(consumed);
    }

    // 判断最终状态：如果是 DONE，表示成功解析完一个请求
    if (parsestate_ == ParseState::DONE)
    {
        return ParseResult::OK;   // 成功
    }

    // 否则数据不足，还需要继续接收
    return ParseResult::NEED_MORE;
}