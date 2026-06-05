#include "logger.h"
#include "threadpool/ThreadPool.h"
#include "Net/Socket.h" //所有要手动释放的都应该封装用raii管理

int main()
{
    MySocket Socket;
    Socket.Socket("ipv4", "tcp");
    sockaddr_in addr{}, aaddr{}; //(记录地址和接口)
    addr.sin_family = AF_INET; // (方式)
    addr.sin_port = htons(8080); // (接口)
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // (地址)
    Socket.Bind(addr);
    Socket.Listen(128);
    ThreadPool test(4);
    while (true)
    {
        int afd = Socket.Accept(aaddr);
        if (afd < 0) continue;//失败跳过
        test.submit([afd](){
            char buf[1024];
            while(true)
            {
                int n = recv(afd, buf, sizeof(buf), 0);
                if (n > 0)send(afd, buf, n, 0);
            }
            INFO (std::string("执行完成"));
            close(afd);
        });//要传递参数
    }
}
