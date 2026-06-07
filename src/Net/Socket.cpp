#include "Net/Socket.h"
#include "logger.h"

MySocket::MySocket()
{
    fd_= -1;
}

MySocket::~MySocket()
{
    if (fd_ > 0)
    close(fd_);
}
void MySocket::Socket(const char* ip, const char* protocol)
{
    //constexpr int ipv4 = PF_INET;//using 给我类型取别名constexpr给常量取别名(都是替换)
    //constexpr int ipv6 = PF_INET6;
    if (std::string(ip) == "ipv4") //不要比较指针,不要粗心
    {
        if(std::string(protocol) == "tcp")
        {
            fd_ = socket(PF_INET, SOCK_STREAM, 0);
        }
        else if (std::string(protocol) == "udp")
        {
            fd_ = socket(PF_INET, SOCK_DGRAM, 0);
        }
        else
        {
            ERROR (std::string("请输入正确的protocol"));
            exit (-1);
        }
    }
    else if (std::string(ip) == "ipv6")
    {
        if(std::string(protocol) == "tcp")
        {
            fd_ = socket(PF_INET6, SOCK_STREAM, 0);
        }
        else if (std::string(protocol) == "udp")
        {
            fd_ = socket(PF_INET6, SOCK_DGRAM, 0);
        }
        else
        {
            ERROR (std::string("请输入正确的protocol"));
            exit (-1);
        }
    }
    else
    {
        ERROR (std::string("请输入正确的ip和protocol"));
        exit (-1);
    }
    if (fd_ < 0)
    {
        ERROR (std::string("创建失败"));
        exit (-1);
    }
    INFO (std::string("创建成功"));
}

void MySocket::Bind(const sockaddr_in& addr)
{
    auto addr_ = (const sockaddr*)&addr; // 统一类类型
    int ib = bind(fd_, addr_, sizeof(addr));
    if (ib < 0)
    {
        ERROR (std::string("绑定失败"));
        exit (-1);
    }
    INFO (std::string("绑定成功"));
}

void MySocket::Listen(const int& nums = 128)
{
    int il = listen(fd_, nums);
    if (il < 0)
    {
        ERROR (std::string("监听失败"));
        exit (-1);
    }
    INFO (std::string("监听成功"));
}

int MySocket::Accept(sockaddr_in& addr)
{
    socklen_t len = sizeof (addr);
    int afd = accept(fd_, (sockaddr*)&addr, &len);//记住把有点诡异,遗留问题吧
    if(afd < 0)
    {
        ERROR (std::string("链接失败"));//不推出保证循环
        return -1;
    }
    INFO("Client connected: " + std::string(inet_ntoa(addr.sin_addr)) 
    + ":" + std::to_string(ntohs(addr.sin_port)));
    //inet_ntoa把二进制转成字符串(能解析ip格式) ntohs(大端转小端)
    return afd;
}
int MySocket::getfd() const
{
    return fd_;
}