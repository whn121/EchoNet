#pragma once
#include "Reactor/Acceptor.h"
#include "Reactor/EventLoop.h"
#include "ThreadPool/IoThreadPool.h"
#include "ThreadPool/WorkThreadPool.h"
#include "Net/Socket.h"
#include <memory>
#include <functional>

// 服务器主类，组装所有组件
class Server 
{
public:
    Server();
    ~Server();
    bool start();   // 启动服务
    void stop();    // 停止服务
    void setcreator (std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)>); //设置对调为了解耦传给iopt

    void setBusinessHandler(IoThreadPool::BusinessHandler handler);
    void setCloseCallback(std::function<void(int)> cb);

    // 要主动暴露主线程网关拿不到不行
    EventLoop* getMainLoop() { return &mainloop_; } // 获得主线程

private:
    Socket listen_socket_;               // 监听套接字
    EventLoop mainloop_;                 // 主事件循环
    std::unique_ptr<Acceptor> acceptor_; // 接受器
    IoThreadPool iopool_;                // IO线程池
    WorkThreadPool workpool_;            // 业务线程池
    std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)> creator_;
    IoThreadPool::BusinessHandler businessHandler_;
    std::function<void(int)> closeCallback_;
};