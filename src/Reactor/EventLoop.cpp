#include "Reactor/EventLoop.h"
#include "Net/Connection.h"
#include <sys/eventfd.h>

EventLoop::EventLoop()
{
    efd_ = epoll_create1 (0);//大于零就行,遗留问题
    wakeup_fd_ = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);//设置非阻塞色,打上关闭标记

    event_.resize(1024);//初始化,要不有可能空地址

    epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd_;
    epoll_ctl (efd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);

    stop_ = false;
}

EventLoop::~EventLoop() 
{
    if (efd_) close (efd_);
    if (wakeup_fd_) close (wakeup_fd_);
}
void EventLoop::loop()
{
    while(!stop_)
    {
        int n = epoll_wait(efd_, event_.data(), 1024, -1);
        for (int i = 0; i < n; i++)
        {
            int varfd = event_[i].data.fd;
            if (varfd == wakeup_fd_)
            {
                uint64_t ok = read (wakeup_fd_, &ok, sizeof (ok));
                //把内置计数器读空,要不然就一直epoll返回
                std::vector<std::function<void()>> task;
                {
                    std::lock_guard<std::mutex> lock (mtx_);
                    task.swap (task_vector_);
                }
                for (auto it : task)
                {
                    it();
                }
            }
            else
            {
                auto it = channel_.find(varfd);//不要move直接给整没了
                if (it != channel_.end()) 
                {
                    it->second->setRevents (event_[i].events);
                    it->second->callBack (event_[i].events);
                }
            }

        }
    }
}

void EventLoop::runInLoop (std::function<void()> task)
{
    std::lock_guard<std::mutex> lock (mtx_);
    task_vector_.emplace_back (std::move (task));
    wakeUp(); // 唤醒
}

void EventLoop::wakeUp ()
{
    uint64_t ok = 1;
    write (wakeup_fd_, &ok, sizeof(ok));
}

void EventLoop::stop ()
{
    stop_ = false;
    wakeUp(); //唤醒方便推出
}

void EventLoop::updateChannel(Channel* channel, uint32_t events)
{
    channel ->setEvents (events);
    epoll_event event = {};
    event.events = events;
    event.data.fd = channel->getFd();
    int ret = epoll_ctl (efd_, EPOLL_CTL_ADD, channel->getFd(), &event);
    if (ret < 0) return;
    channel_.emplace(channel->getFd(), channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    epoll_event event = {};
    event.data.fd = channel->getFd();
    epoll_ctl (efd_, EPOLL_CTL_DEL, channel->getFd(), &event);
    channel_.erase(channel->getFd());

}

void EventLoop::updateConnection(std::shared_ptr<Connection> connection)
{
    connections_.emplace(connection->getChannel()->getFd(), move(connection));
}

void EventLoop::removeConnection(int fd) {
    // 先从 epoll 中删除
    epoll_event event = {};
    event.data.fd = fd;
    epoll_ctl(efd_, EPOLL_CTL_DEL, fd, &event);
    // 再清理本地映射
    channel_.erase(fd);
    connections_.erase(fd);
}

void EventLoop::updateChannelEvent (Channel* channel, uint32_t events)
{
    epoll_event event = {};
    event.events = events;
    event.data.fd = channel->getFd();
    epoll_ctl (efd_, EPOLL_CTL_MOD, channel->getFd(), &event);
}