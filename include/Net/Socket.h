#pragma one
#include <sys/socket.h>   //socket核心
#include <netinet/in.h>   //sockaddr结构体和端口宏(如IPPROTO_TCP)
#include <arpa/inet.h>    //inet_addr/inet_ntoa地址转换
#include <unistd.h>       //close()
#include "logger.h"


class MySocket
{
public:
    MySocket();
    ~MySocket();

private:
    int fd_;

public:
    void Socket(const char* Ip, const char* protocol);
    void Bind(const sockaddr_in& addr);
    void Listen( const int& nums);
    int Accept(sockaddr_in& addr);
    int getfd()const; //connection要用
};