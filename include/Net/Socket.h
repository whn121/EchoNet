#pragma once

#include <sys/socket.h>
#include <netinet/in.h> 
#include <unistd.h>
#include <string>
#include <fcntl.h> //操作fd
#include <arpa/inet.h> // inet_pton 把字符串变成网络二进制ip


enum class IP { ipv4, ipv6 };
enum class Proto { tcp, udp };

// 简单socket封装（目前仅支持TCP）
class Socket 
{
public:
    Socket();
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool m_init(IP ip, Proto proto);                    // 创建socket
    bool m_bind(uint16_t port = 8080, uint32_t addr = INADDR_ANY); // 绑定
    bool m_listen(int backlog = 128);                   // 监听
    int m_accept();                                     // 接受连接
    void setReuseAddr(bool on);                         // 设置地址重用
    void setNonBlocking();                              // 设置非阻塞
    int getFd() const { return fd_; };                  // 获取文件描述符

    // 提供主动链接,网关要服务器主动连接诶
    bool m_connect(const std::string& host, uint16_t port);   // 主动连接

    // 交出fd所有权,要不gatewaymin里析构旧弹错误
    int release(); 

private:
    int fd_ = -1;
    std::string ip_;       // "ipv4" 或 "ipv6"
    sockaddr_in addr;      // 地址结构

    // 服务主动连接
    bool parseAddr(const std::string& host, uint16_t port);   // 解析地址

};