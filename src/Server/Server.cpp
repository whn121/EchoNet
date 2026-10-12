#include "Server/Server.h"
#include "Logger/AsyncLogger.h"
#include "Common/Config.h"
#include "Common/SignalHandler.h"


Server::Server()
    : iopool_(Config::instance().io_threads),
    workpool_(Config::instance().work_threads) {}

Server::~Server()
{
    stop();
}

bool Server::start() 
{
    //为了解耦吧函数传给iopt
    if (creator_) iopool_.setbuildconn (creator_);
    // 创建监听socket
    if (!listen_socket_.m_init(IP::ipv4, Proto::tcp)) 
    {
        LOG_INFO("socket创建失败"); return false;
    }
    listen_socket_.setReuseAddr(true);

    uint16_t port = Config::instance().port;
    if (!listen_socket_.m_bind(port)) { LOG_INFO("绑定失败"); return false; }
    if (!listen_socket_.m_listen())   { LOG_INFO("监听失败"); return false; }

    LOG_INFO("监听成功，端口: " + std::to_string(port));

    // 创建Acceptor，设置新连接回调
    acceptor_ = std::make_unique<Acceptor>(listen_socket_.getFd(), &mainloop_, &iopool_);
    acceptor_->setCallBack([this](int afd) { iopool_.submit(afd); });
    acceptor_->listen();

    // 设置IO线程池的工作回调：将Task投递给业务线程池
    iopool_.setCallBack([this](Task t) { workpool_.submit(std::move(t)); });
    
    // 转发业务处理器给 IoThreadPool
    if (businessHandler_) iopool_.setBusinessHandler(businessHandler_);
    if (closeCallback_)   iopool_.setCloseCallback(closeCallback_);

    // 注册信号处理：收到SIGINT/SIGTERM时停止主循环
    SignalHandler::init([this] { mainloop_.stop(); });

    // 启动主事件循环（阻塞）
    mainloop_.loop();

    LOG_INFO("主循环退出，开始关闭...");
    return true;
}



void Server::stop()
{
    bool expected = false;

    // 整个停服流程只允许一个调用者执行。
    if (!stopStarted_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return;
    }

    // 1. 确保主事件循环停止。
    // 当前正常调用路径是 start() 已经返回。
    mainloop_.stop();

    // 2. 注销监听Channel, 再注销Acceptor
    if (acceptor_)
    {
        acceptor_->stop();
        acceptor_.reset();
    }

    // 3. 释放监听socket
    listen_socket_.close();

    // 4. 在io循环运行时, 让现有连接进入关闭流程
    LOG_INFO("Server stopping: closing all connections");
    iopool_.closeAllConnections();

    // 5. 停止并等待业务线程池
    workpool_.stop();

    // 6. 等待业务任务产生的io收尾回调完成
    LOG_INFO("Server stopping: draining I/O callbacks");
    iopool_.waitForPendingCallbacks();

    // 7. 最后停止并join io 线程池
    iopool_.stop();

    LOG_INFO("Server stopped");
}



void Server::setcreator (std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)> creator)
{
    creator_ = creator;
}

void Server::setBusinessHandler(IoThreadPool::BusinessHandler handler)
{
    businessHandler_ = std::move(handler);
}

void Server::setCloseCallback(std::function<void(int)> cb)
{
    closeCallback_ = std::move(cb);
}