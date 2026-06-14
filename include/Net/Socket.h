#pragma once

#include <sys/socket.h>
#include <netinet/in.h> 
#include <unistd.h>
#include <string>
#include <fcntl.h> //操作fd

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
    bool socket_(const char* ip, const char* stream);
    bool bind_(uint16_t sin_port_ = htonl (8080), uint32_t s_addr_ = htonl (INADDR_ANY));//从后面开始默认
    bool listen_(int nums = 128);
    int accept_();

    void setReuseAddr(bool on);

    void setNonBlocking(int fd);
    int getFd()const;

};