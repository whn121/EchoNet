#pragma once
#include "Reactor/Acceptor.h"
#include "Reactor/EventLoop.h"
#include "ThreadPool/IoThreadPool.h"
#include "ThreadPool/WorkThreadPool.h"
#include "Net/Socket.h"
#include <memory>

// 服务器主类，组装所有组件
class Server {
public:
    Server();
    ~Server();
    bool start();   // 启动服务
    void stop();    // 停止服务
private:
    Socket listen_socket_;               // 监听套接字
    EventLoop mainloop_;                 // 主事件循环
    std::unique_ptr<Acceptor> acceptor_; // 接受器
    IoThreadPool iopool_;                // IO线程池
    WorkThreadPool workpool_;            // 业务线程池
};