#pragma once
#include "Reactor/Acceptor.h"
#include "Reactor/EventLoop.h"
#include "ThreadPool/IoThreadPool.h"
#include "ThreadPool/WorkThreadPool.h"
#include "Net/Socket.h"
#include "Server/Server.h"


class Server
{
public:
    Server(int ionums = 4, int worknums = 4);
    ~Server();

private:
    Socket listen_socket_; //保证监听fd的生命周期

    EventLoop mainloop_;
    Acceptor acceptor_;

    IoThreadPool iopool_;
    WorkThreadPool workpool_;

    int iopoolnums_;
    int workpoolnums_;

public:
    bool start();

};