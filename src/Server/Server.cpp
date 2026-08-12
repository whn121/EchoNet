#include "Server/Server.h"
#include "Logger/logger.h"
#include "Common/Config.h"
#include "Common/SignalHandler.h"

Server::Server()
    : iopool_(Config::instance().io_threads),
      workpool_(Config::instance().work_threads) {}

Server::~Server() = default;

bool Server::start() {
    // 创建监听socket
    if (!listen_socket_.m_init(IP::ipv4, Proto::tcp)) {
        INFO("socket创建失败"); return false;
    }
    listen_socket_.setReuseAddr(true);
    listen_socket_.setNonBlocking();

    uint16_t port = Config::instance().port;
    if (!listen_socket_.m_bind(port)) { INFO("绑定失败"); return false; }
    if (!listen_socket_.m_listen())   { INFO("监听失败"); return false; }

    INFO("监听成功，端口: " + std::to_string(port));

    // 创建Acceptor，设置新连接回调
    acceptor_ = std::make_unique<Acceptor>(listen_socket_.getFd(), &mainloop_, &iopool_);
    acceptor_->setCallBack([this](int afd) { iopool_.submit(afd); });
    acceptor_->listen();

    // 设置IO线程池的工作回调：将Task投递给业务线程池
    iopool_.setCallBack([this](Task t) { workpool_.submit(std::move(t)); });

    // 注册信号处理：收到SIGINT/SIGTERM时停止主循环
    SignalHandler::init([this] { mainloop_.stop(); });

    // 启动主事件循环（阻塞）
    mainloop_.loop();

    INFO("主循环退出，开始关闭...");
    return true;
}

void Server::stop() {
    mainloop_.stop();
    iopool_.stop();
    workpool_.stop();
}