#pragma once

#include <sys/socket.h>
#include <netinet/in.h> 
#include <unistd.h>
#include <string>
#include <fcntl.h> //操作fd

    enum class IP {ipv4, ipv6};
    enum class Proto {tcp, udp};

class Socket
{
public:
    Socket();
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator= (const Socket&) = delete;

private:
    int fd_;
    std::string ip_; //记录ip类型,方便bind 
    sockaddr_in addr;

public:
    bool m_init(IP ip, Proto proto);
    bool m_bind(uint16_t sin_port_ = 8080, uint32_t s_addr_ = INADDR_ANY);//从后面开始默认
    bool m_listen(int nums = 128);
    int m_accept();

    void setReuseAddr(bool on);

    void setNonBlocking();
    int getFd()const;

};