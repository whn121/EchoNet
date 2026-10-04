#include "Net/Socket.h"
#include "Logger/AsyncLogger.h"
#include <cstring>
#include <cerrno>    // 新增：errno
#include <iostream>
#include <poll.h>


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

bool Socket::parseAddr(const std::string& host, uint16_t port)
{
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // 支持ipv4
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
    {
        std::cerr << "inet_pton failed: " << host << std::endl;
        return false;
    }
    return true;
}

bool Socket::m_connect(const std::string& host, uint16_t port)
{
    // 1.创建
    if (!m_init(IP::ipv4, Proto::tcp)) return false;

    // 2.解析
    if(!parseAddr(host, port)) return false;

    // 3.主动连接
    int ret = ::connect(fd_, reinterpret_cast<sockaddr*> (&addr), sizeof(addr));

    // EINPROGRESS 是正常状态，不是错误
    if (ret < 0 && errno != EINPROGRESS)
    {
        std::cerr << "connect failed to " << host << ":" << port << " errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    // 4.如果是EINPROGRESS, 用poll等待链接完成
    if (ret < 0)
    {
        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLOUT;

        int n = ::poll(&pfd, 1, 3000); //最多等三秒
        if (n <= 0)
        {
            std::cerr << "connect timeout to " << host << ":" << port << std::endl;
            return false;
        }

        // 检查 SO_ERROR
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0)
        {
            std::cerr << "connect SO_ERROR=" << err << std::endl;
            return false;
        }
    }

    return true;
}

int Socket::release()
{
    int fd = fd_;
    fd_ = -1;   // 让析构不 close
    return fd;
}
