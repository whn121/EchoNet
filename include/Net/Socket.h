#pragma once

#include <sys/socket.h>
#include <netinet/in.h> 
#include <unistd.h>
#include <string>
#include <fcntl.h> //操作fd

enum class IP { ipv4, ipv6 };
enum class Proto { tcp, udp };

// 简单socket封装（目前仅支持TCP）
class Socket {
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
    int getFd() const;                                  // 获取文件描述符
private:
    int fd_ = -1;
    std::string ip_;       // "ipv4" 或 "ipv6"
    sockaddr_in addr;      // 地址结构
};