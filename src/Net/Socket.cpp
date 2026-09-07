#include "Net/Socket.h"
#include "Logger/logger.h"

Socket::Socket() : fd_(-1), ip_(""), addr{} {}
Socket::~Socket() { if (fd_ > 0) close(fd_); }

bool Socket::m_init(IP ip, Proto proto) 
{
    if (ip == IP::ipv4 && proto == Proto::tcp) 
    {
        fd_ = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); //直接生成非阻塞
        ip_ = "ipv4";
    } 
    else if (ip == IP::ipv4 && proto == Proto::udp) 
    {
        fd_ = socket (AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        ip_ = "ipv4";
    } 
    else return false;
    if (fd_ == -1)
    {
        LOG_ERROR("socket create failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool Socket::m_bind(uint16_t port, uint32_t addr_val) 
{
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(addr_val);
    return bind(fd_, (const sockaddr*)&addr, sizeof(addr)) >= 0;
}

bool Socket::m_listen(int backlog) 
{
    return listen(fd_, backlog) >= 0;
}

int Socket::m_accept() 
{
    sockaddr_in client_addr{};
    socklen_t len = sizeof (client_addr);
    int afd = accept4 (fd_, (sockaddr*)&client_addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (afd == -1)
    {
        LOG_ERROR("accept failed: " + std::string(strerror(errno)));
    }
    return afd;
}

void Socket::setReuseAddr(bool on) 
{
    int opt = on ? 1 : 0;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void Socket::setNonBlocking() //函数保留防止以后不兼容时使用,我这里没调用
{
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

int Socket::getFd() const { return fd_; }