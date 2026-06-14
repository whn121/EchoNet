#include "Reactor/EventLoop.h"
#include "Logger/logger.h"//别忘了删

EventLoop::EventLoop()
{
    efd_ = epoll_create1 (0);//大于零就行,遗留问题
    event_.resize(1024);//初始化,要不有可能空地址
    stop_ = false;
}

EventLoop::~EventLoop()
{
    close (efd_);
}

void EventLoop::loop()
{
    while(true)
    {
        INFO("进入loop循环");
        int n = epoll_wait(efd_, event_.data(), 1024, -1);
        for (int i = 0; i < n; i++)
        {
            INFO("进入for循环");
            int varfd = event_[i].data.fd;
            INFO(varfd);
            uint32_t varevent = event_[i].events;
            auto it = channel_.find(varfd);//不要move直接给整没了
            if (it != channel_.end()) 
            {
                it->second->callBack(varevent);
            }
        }
    }
}

void EventLoop::updateChannel(Channel* channel)
{
    epoll_event event = {};
    event.events = channel->getEvent();
    event.data.fd = channel->getFd();
    int ret = epoll_ctl (efd_, EPOLL_CTL_ADD, channel->getFd(), &event);
    if (ret < 0) ERROR("epoll_ctl ADD failed");
    channel_.emplace(channel->getFd(), channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    epoll_event event = {};
    event.events = channel->getEvent();
    event.data.fd = channel->getFd();
    epoll_ctl (efd_, EPOLL_CTL_DEL, channel->getFd(), &event);
    channel_.erase(channel->getFd());

}
