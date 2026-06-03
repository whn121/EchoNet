#include <iostream>
#include <sys/socket.h>   //socket核心
#include <netinet/in.h>   //sockaddr结构体和端口宏(如IPPROTO_TCP)
#include <arpa/inet.h>    //inet_addr/inet_ntoa地址转换
#include <unistd.h>       //close()
#include <string>
#include <cstring>
#include <cerrno>
#include "logger.h"

int main()
{
    int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP); //(ipv4, 流式传输, tcp(可以写0))
    if (fd < 0)
    {
        ERROR (std::string("socket失败") + strerror(errno));
        return -1;
    }
    INFO ("socket创建成功");
    sockaddr_in addr{}, aaddr{}; //(记录地址和接口)
    addr.sin_family = AF_INET; // (方式)
    addr.sin_port = htons(8080); // (接口)
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // (地址)
    auto addr_ = (const sockaddr*)&addr; // 统一类类型
    int ifd = bind (fd, addr_, sizeof(addr)); //大小一定是数据的不是指针的无敌了
    if (ifd < 0)
    {
        ERROR (std::string("绑定失败") + strerror(errno));
        return -1;
    }
    INFO ("绑定成功");
    int ifl = listen(fd, 128); //设置同时与服务器建立链接的上限数 (fd, 最大连接数(小的自动优化为128)) 
    if (ifl < 0)
    {
        ERROR (std::string("监听失败") + strerror(errno));
        return -1;
    }
    INFO ("监听成功");
    char buf[1024];
    INFO("等待链接");
    while (true)
    {
        socklen_t aaddrlen = sizeof (aaddr); // 接受要这个类型的指针
        int afd = accept(fd, (sockaddr*)&aaddr,&aaddrlen); //accept4有啥用?(fd, 传出参数addr, 传入传出值)
        if(afd < 0)
        {
            ERROR (std::string("链接失败") + strerror(errno));
            continue;
        }
        INFO("Client connected: " + std::string(inet_ntoa(aaddr.sin_addr)) 
        + ":" + std::to_string(ntohs(aaddr.sin_port)));
        //inet_ntoa把二进制转成字符串(能解析ip格式) ntohs(大端转小端)
        int n;
        while((n = recv (afd, buf, 1024, 0)) > 0)
        {
            send (afd, buf, n, 0);
        }
        if (afd >= 0) close (afd);
        INFO ("连接断开");
    }
    close (fd);// 先关闭afd更安全
}
