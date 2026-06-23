#pragma once
#include "Net/Buffer.h"
#include <unistd.h>
#include <sys/socket.h> 
#include "Reactor/Channel.h"
#include <memory>
#include "Reactor/EventLoop.h"
#include "HTTP/HttpContext.h"
#include "HTTP/HttpService.h"
#include "ThreadPool/Task.h"


class Connection : public std::enable_shared_from_this <Connection>
{
public:
    Connection(int afd, std::unique_ptr<Channel> cahnnel, EventLoop* loop);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator= (const Connection&) = delete;

private:
    int afd_;
    EventLoop* loop_;
    Buffer inBuffer_;
    Buffer outBuffer_;
    std::string memory_;
    std::unique_ptr<Channel> channel_;
    std::function<void(int)> closeCallBack_;

    HttpRequest httprequest_;
    HttpContext httpcontext_;
    HttpService httpservice_;

    std::function<void(Task)> worksumbitcallback_;

public:
    void read();
    void write();
    void setResponse(const HttpResponse& httpresponse);
    Channel* getChannel();
    void setcloseCallback(std::function<void(int)> close);
    void myClose();
    void init(); //用来处理回调传递给channel

    void setCallBack(std::function<void(Task)>);
    HttpResponse handle(HttpRequest);

};
