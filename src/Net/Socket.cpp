#include "Net/Socket.h"

Socket::Socket()
{
    fd_ = -1;
    ip_ = "";
    addr = {};
}

Socket::~Socket()
{
    if (fd_ > 0)
    close (fd_);
}

bool Socket::m_init(IP ip, Proto proto)
{
    if (ip == IP::ipv4)
    {
        if (proto == Proto::tcp)
        {
            fd_ = socket (AF_INET, SOCK_STREAM, 0);
            ip_ = "ipv4";
        }
        else if (proto == Proto::udp)
        {
            fd_ = socket (AF_INET, SOCK_DGRAM, 0);
            ip_ = "ipv4";
        }
        else 
        {
            return false;
        }
    }
    else if (ip == IP::ipv6)
    {
        if (proto == Proto::tcp)
        {
            fd_ = socket (AF_INET6, SOCK_STREAM, 0);
            ip_ = "ipv6";
        }
        else if (proto == Proto::udp)
        {
            fd_ = socket (AF_INET6, SOCK_DGRAM, 0);
            ip_ = "ipv6";
        }
        else 
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    return fd_ > 0;
}

bool Socket::m_bind(uint16_t sin_prot_, uint32_t s_addr_)
{
    if (ip_ == "ipv4")
    {
        addr.sin_family = AF_INET;
    }
    else if (ip_ == "ipv6")
    {
        addr.sin_family = AF_INET6;
    }
    else
    {
        return false;
    }
    addr.sin_port = htons (sin_prot_);
    addr.sin_addr.s_addr = htonl (s_addr_);
    int lfb = bind (fd_, (const sockaddr*)&addr, sizeof(addr));
    if (lfb < 0)
    {
        return false;
    }
    return true;
}

bool Socket::m_listen(int nums)
{
    int lfl = listen (fd_, nums);
    if (lfl < 0)
    {
        return false;
    }
    return true;
}
int Socket::m_accept()
{
    sockaddr_in aaddr = {};
    socklen_t len = sizeof (aaddr);
    int afd = accept(fd_, (sockaddr*)&aaddr, &len);
    if (afd < 0)
    {
        return -1;
    }
    else if (afd == 0)
    {
        return 0;
    } 
    else
    {
        return afd;
    }
}

void Socket::setReuseAddr(bool on)
{
    int sra = on ? 1 : 0;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &sra, sizeof(sra)); //端口复用,防止之前结束后不能立即重启的
}

void Socket::setNonBlocking()
{

    int flag = fcntl(fd_, F_GETFL, 0);//获得状态标志
    fcntl(fd_, F_SETFL, flag | O_NONBLOCK);//设置非阻塞
}

int Socket::getFd() const
{
    return fd_;
}