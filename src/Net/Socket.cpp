#include "Net/Socket.h"

Socket::Socket() : fd_(-1), ip_(""), addr{} {}
Socket::~Socket() { if (fd_ > 0) close(fd_); }

bool Socket::m_init(IP ip, Proto proto) {
    if (ip == IP::ipv4) {
        if (proto == Proto::tcp) {
            fd_ = socket(AF_INET, SOCK_STREAM, 0); ip_ = "ipv4";
        } else if (proto == Proto::udp) {
            fd_ = socket(AF_INET, SOCK_DGRAM, 0); ip_ = "ipv4";
        } else return false;
    } else if (ip == IP::ipv6) {
        // 预留IPv6，暂未实现
        return false;
    } else return false;
    return fd_ > 0;
}

bool Socket::m_bind(uint16_t port, uint32_t addr_val) {
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(addr_val);
    return bind(fd_, (const sockaddr*)&addr, sizeof(addr)) >= 0;
}

bool Socket::m_listen(int backlog) {
    return listen(fd_, backlog) >= 0;
}

int Socket::m_accept() {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int afd = accept(fd_, (sockaddr*)&client_addr, &len);
    return afd < 0 ? -1 : afd;
}

void Socket::setReuseAddr(bool on) {
    int opt = on ? 1 : 0;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void Socket::setNonBlocking() {
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

int Socket::getFd() const { return fd_; }