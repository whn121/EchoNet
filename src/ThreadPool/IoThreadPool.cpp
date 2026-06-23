#include "ThreadPool/IoThreadPool.h"


IoThreadPool::IoThreadPool (int num) : nums_ (num), stop_ (false)
{
    for (int i = 0; i < num; i++)
    {
        loops_.emplace_back (std::make_unique<EventLoop>());
        thread_.emplace_back ([this, i]{worker(i);});
    }
}

IoThreadPool::~IoThreadPool ()
{
    stop();
}

void IoThreadPool::submit (int afd)
{
    int idx = next_++ % nums_;
    EventLoop* loop = loops_[idx].get();

    auto newChannel = std::make_unique<Channel>(afd);
    auto conn = std::make_shared<Connection>(afd, std::move(newChannel), loop);

    conn->init();

    
    conn -> setcloseCallback([l = loop](int fd){l->removeConnection(fd);});
    conn -> setCallBack (giveconn());
    loop-> updateChannel(conn->getChannel(), EPOLLIN | EPOLLOUT);
    loop-> updateConnection(move(conn));
}

void IoThreadPool::worker (int i)
{
    loops_[i]->loop();
}

void IoThreadPool::stop ()
{
    stop_ = true;
    for (auto& it : loops_)
    {
        it -> stop();
    }
    for (auto& it : thread_)//必须引用
    {
        if (it.joinable()) 
        it.join();
    }

    close (efd_);
}

void IoThreadPool::setCallBack (std::function<void(Task)> callback)
{
    giveconncallback_ = callback;
}

std::function<void(Task)> IoThreadPool::giveconn ()
{
    return giveconncallback_;
}
