#include "Net/Connection.h"
#include <sys/socket.h>
#include <unistd.h>

Connection::Connection(int afd)
{
    afd_ = afd;
}
Connection::~Connection()
{
    close(afd_);
}

bool Connection::read()
{
    char buf[1024];
    int n = recv (afd_, buf, sizeof(buf), 0);
    if (n == 0 )
    {
        INFO(std::string("客户端断开链接"));
        return false;
    }
    else if (n < 0)
    {
        if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)//这些是正常错误只是没有数据了
        {
            return true;//只是没数据了,继续等待,
        }
        else
        {
            ERROR(std::string("接受错误"));
            return false;
        }
    }
    buffer_.bappend(buf, n);
    return true;
}

void Connection::write(const std::string& msg)
{
    send(afd_, msg.c_str(), msg.size(), 0);//第二个参数是多少元素,不是大小
}

Buffer& Connection::getbuffer() //tcp本质是在同一个流,不能拷贝,应该禁止拷贝
{
    return buffer_;
}