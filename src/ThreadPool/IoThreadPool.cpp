#include "ThreadPool/IoThreadPool.h"
#include "Net/Connection.h"
#include "Protocol/HttpProtocol.h"
#include "ChatService/ChatService.h"

IoThreadPool::IoThreadPool(int num) : num_(num > 0 ? num : 8) 
{
    // 第二步: 创造所以Eventloop
    for (int i = 0; i < num; ++i)
    {
        loops_.emplace_back(std::make_unique<EventLoop>());
    }
    //第二步: 启动所有工作线程
    for (int i = 0; i < num; ++i)
    {
        threads_.emplace_back([this, i] { worker(i); });
    }
}

IoThreadPool::~IoThreadPool() { stop(); }

void IoThreadPool::submit(int afd) 
{
    int idx = next_++ % num_;                     // 轮询选择EventLoop
    EventLoop* loop = loops_[idx].get();

    // 把“创建连接并注册”整个任务投递到子线程 
    loop->runInLoop([this, loop, afd, workCallback = workCallback_] 
    {
        //auto channel = std::make_unique<Channel>(afd);
        //auto protocol = std::make_unique<HttpProtocol>();
        //auto conn = std::make_shared<Connection>(afd, std::move(channel), loop, std::move(protocol));
        //解耦了创还能函数在main里现在回调了,上面是之前的代码

        if (buildconn_)
        {
            auto conn = buildconn_ (afd, loop);

            conn->init();
            conn->setcloseCallback([loop](int fd) { ChatService::instance().onConnectionClosed(fd); loop->removeConnection(fd); });
            conn->setCallBack(workCallback);

            loop->updateChannel(conn->getChannel(), EPOLLIN);
            loop->updateConnection(conn);

        } 
    });
}

void IoThreadPool::worker(int idx) 
{
    loops_[idx]->loop();   // 启动事件循环（阻塞）
}

void IoThreadPool::stop() 
{
    bool expected = false;
    if (!stop_.compare_exchange_strong (expected, true)) return; //比较并交换如果与1相当与2交换返回true,不相等吧原子赋值给1
    for (auto& loop : loops_) loop->stop();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

void IoThreadPool::setCallBack(std::function<void(Task)> cb) { workCallback_ = cb; }
std::function<void(Task)> IoThreadPool::getCallback() { return workCallback_; }


void IoThreadPool::setbuildconn (std::function<std::shared_ptr<Connection> (int afd, EventLoop* loop)> bconn)
{
    buildconn_ = bconn;
}