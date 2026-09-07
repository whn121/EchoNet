#include "Reactor/EventLoop.h"
#include "Net/Connection.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include "Logger/logger.h"


EventLoop::EventLoop() : owner_thread_id_ (std::this_thread::get_id())
{
    efd_ = epoll_create1(0);  // 创建epoll实例
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); // 创建eventfd
    events_.resize(1024);

    // 将wakeup_fd_注册到epoll，监听读事件
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd_;
    epoll_ctl(efd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);
}

EventLoop::~EventLoop() 
{
    if (efd_ != -1) close(efd_);
    if (wakeup_fd_ != -1) close(wakeup_fd_);
}

void EventLoop::loop() 
{
    owner_thread_id_ = std::this_thread::get_id();   // 记录实际运行 loop 的线程
    
    while (!stop_) 
    {
        // 阻塞等待事件，-1表示无限等待
        int n = epoll_wait(efd_, events_.data(), events_.size(), -1);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;  // 被信号中断，继续等待
            }
            LOG_ERROR("epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }
        for (int i = 0; i < n; ++i) 
        {
            int fd = events_[i].data.fd;
            if (fd == wakeup_fd_) 
            {
                // 唤醒事件：读出数据清空计数器，然后执行投递的任务
                uint64_t dummy;
                read(wakeup_fd_, &dummy, sizeof(dummy));
                std::vector<std::function<void()>> tasks;
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    tasks.swap(task_queue_);
                }
                for (auto& task : tasks) task();
            }
            else 
            {
                auto it = channels_.find(fd);
                if (it != channels_.end()) 
                {
                    it->second->setRevents(events_[i].events);
                    it->second->handleEvent(events_[i].events); // 执行回调
                }
            }
        }

        //本次 epoll_wait 返回的所有事件处理完毕，释放待销毁连接
        pendingConnections_.clear();
    }
}

void EventLoop::runInLoop(std::function<void()> cb) 
{
    if (isInLoopThread()) 
    {
        cb();   // 直接执行
    }
    else
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            task_queue_.emplace_back(std::move(cb));
        }
        wakeUp();  // 唤醒epoll，让其立即处理任务
    }
}

void EventLoop::wakeUp() 
{
    uint64_t one = 1;
    write(wakeup_fd_, &one, sizeof(one));
}

void EventLoop::stop() 
{
    stop_ = true;
    wakeUp();  // 唤醒，让循环退出
}

void EventLoop::updateChannel(Channel* ch, uint32_t events) 
{
    assertInLoopThread();   // 确保在所属线程

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = ch -> getFd();

    //要进行返回值检测
    int ret = epoll_ctl(efd_, EPOLL_CTL_ADD, ch->getFd(), &ev);
    if (ret < 0)
    {
        int saved_errno = errno;
        std::string log_str = "epoll_ctl ADD failed, fd=" + std::to_string(ch->getFd())
        + " errno=" + std::to_string(saved_errno)
        + " (" + std::string(strerror(saved_errno)) + ")";
        LOG_ERROR(log_str);
        return ; //失败不加入channels_;
    }

    ch -> setEvents (events);
    channels_[ch->getFd()] = ch;
}

void EventLoop::removeChannel(Channel* ch) 
{
    assertInLoopThread();   // 确保在所属线程

    int ret = epoll_ctl(efd_, EPOLL_CTL_DEL, ch->getFd(), nullptr);
    if (ret < 0)
    {
        int saved_errno = errno;
        std::string log_str = "epoll_ctl DEL failed, fd=" + std::to_string(ch->getFd())
        + " errno=" + std::to_string(saved_errno)
        + " (" + std::string(strerror(saved_errno)) + ")";
        LOG_ERROR(log_str);
    }
    ch -> setEvents (0); //清空内部状态
    channels_.erase(ch->getFd());
}

void EventLoop::updateConnection(std::shared_ptr<Connection> conn) 
{
    assertInLoopThread();   // 确保在所属线程

    connections_[conn->getChannel()->getFd()] = conn;
}

void EventLoop::removeConnection(int fd) 
{
    assertInLoopThread();   // 确保在所属线程

    auto it = channels_.find (fd);
    if (it != channels_.end ())
    {
        it -> second -> setEvents (0);
    }

    int ret = epoll_ctl(efd_, EPOLL_CTL_DEL, fd, nullptr);
    if (ret < 0)
    {
        int saved_errno = errno;
        std::string log_str = "epoll_ctl DEL failed, fd=" + std::to_string(fd)
        + " errno=" + std::to_string(saved_errno)
        + " (" + std::string(strerror(saved_errno)) + ")";
        LOG_ERROR(log_str);
    }
    channels_.erase(fd);
    //移除 Connection 映射，但暂存 shared_ptr，延迟析构
    auto connit = connections_.find (fd);
    if (connit != connections_.end())
    {
        pendingConnections_.push_back(connit->second);
        connections_.erase(connit);
    }
}

void EventLoop::updateChannelEvent(Channel* ch, uint32_t events) 
{
    assertInLoopThread();   // 确保在所属线程

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = ch->getFd();

    int ret = epoll_ctl(efd_, EPOLL_CTL_MOD, ch->getFd(), &ev);
    if (ret < 0)
    {
        int saved_errno = errno;
        std::string log_str = "epoll_ctl MOD failed, fd=" + std::to_string(ch->getFd())
        + " errno=" + std::to_string(saved_errno)
        + " (" + std::string(strerror(saved_errno)) + ")";
        LOG_ERROR(log_str);
    }

    ch->setEvents(events);  
}

void EventLoop::assertInLoopThread() const 
{
    if (std::this_thread::get_id() != owner_thread_id_) 
    {
        LOG_ERROR("EventLoop method called from wrong thread!");
        abort();   // 直接终止，因为继续执行会导致数据竞争
    }
}

bool EventLoop::isInLoopThread() const 
{
    return std::this_thread::get_id() == owner_thread_id_;
}