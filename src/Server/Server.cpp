#include "Server/Server.h"
#include "Logger/logger.h"

Server::Server(int ionums, int worknums) 
: acceptor_ (0, nullptr, nullptr)
, iopoolnums_ (ionums), workpoolnums_ (worknums)
, iopool_ (ionums), workpool_ (worknums)
{}

Server::~Server() = default;

bool Server::start ()
{
    bool ifs = listen_socket_.m_init(IP::ipv4, Proto::tcp);
    if (!ifs)
    {
        ERROR("创建失败");
    }
    INFO("创建成功");

    listen_socket_.setReuseAddr(1);
    listen_socket_.setNonBlocking();

    bool ifb = listen_socket_.m_bind(8080);
    if (!ifb)
    {
        ERROR("绑定失败");
        return false;
    }
    INFO("绑定成功");
    bool ifl= listen_socket_.m_listen();
    if (!ifl)
    {
        ERROR("监听失败");
        return false;
    }
    INFO("监听成功");
    INFO("等待链接");

    acceptor_ = std::move (Acceptor (listen_socket_.getFd(), &mainloop_, &iopool_));
    acceptor_.setCallBack ([it = this](int afd){it -> iopool_.submit(afd);});
    acceptor_.listen ();

    iopool_.setCallBack ([it = this](Task task){it -> workpool_.submit_(task);});

    mainloop_.loop ();

    return true;
}
