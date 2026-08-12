#include "ThreadPool/IoThreadPool.h"
#include "Net/Connection.h"
#include "Protocol/HttpProtocol.h"

IoThreadPool::IoThreadPool(int num) : num_(num) {
    for (int i = 0; i < num; ++i) {
        loops_.emplace_back(std::make_unique<EventLoop>());
        threads_.emplace_back([this, i] { worker(i); });
    }
}

IoThreadPool::~IoThreadPool() { stop(); }

void IoThreadPool::submit(int afd) {
    int idx = next_++ % num_;                     // 轮询选择EventLoop
    EventLoop* loop = loops_[idx].get();

    auto channel = std::make_unique<Channel>(afd);
    auto protocol = std::make_unique<HttpProtocol>();  // 创建HTTP协议对象
    auto conn = std::make_shared<Connection>(afd, std::move(channel), loop, std::move(protocol));

    conn->init();
    conn->setcloseCallback([loop](int fd) { loop->removeConnection(fd); });
    conn->setCallBack(workCallback_);            // 设置业务回调
    loop->updateChannel(conn->getChannel(), EPOLLIN);
    loop->updateConnection(conn);
}

void IoThreadPool::worker(int idx) {
    loops_[idx]->loop();   // 启动事件循环（阻塞）
}

void IoThreadPool::stop() {
    if (stop_) return;
    stop_ = true;
    for (auto& loop : loops_) loop->stop();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

void IoThreadPool::setCallBack(std::function<void(Task)> cb) { workCallback_ = cb; }
std::function<void(Task)> IoThreadPool::getCallback() { return workCallback_; }