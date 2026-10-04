#include "Reactor/EventLoop.h"
#include "Net/Connection.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include "Logger/AsyncLogger.h"
#include <sys/timerfd.h>



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

    // 每 5 秒扫描一次，检查超过 60 秒无活动的连接
    setTimer(5, [this] { checkIdleConnections(60);});
}

EventLoop::~EventLoop()
{
    // timerChannel_ 的注销依赖 efd_，
    // 所以必须在关闭 efd_ 之前执行。
    if (timerChannel_)
    {
        removeChannel(timerChannel_.get());
        timerChannel_.reset();
    }

    // timerChannel 已经从 epoll 中移除，
    // 现在可以关闭 timerfd。
    if (timerfd_ != -1)
    {
        ::close(timerfd_);
        timerfd_ = -1;
    }

    // wakeup_fd_ 已经不再需要。
    if (wakeup_fd_ != -1)
    {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }

    // 最后关闭 epoll 实例。
    if (efd_ != -1)
    {
        ::close(efd_);
        efd_ = -1;
    }
}

void EventLoop::loop() 
{
    owner_thread_id_ = std::this_thread::get_id();   // 记录实际运行 loop 的线程
    
    // 上一轮待销毁的连接，放在这里析构
    // 避免 pendingConnections_.clear() 一次性析构所有 shared_ptr 造成 IO 线程卡顿
    std::vector<std::shared_ptr<Connection>> garbage;

    while (!stop_) 
    {
        // 上一轮的 garbage 在这里析构
        garbage.clear();

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

        // 关键：如果事件数达到上限，下次扩容
        // 防止 events_ 固定 1024 导致高并发时每次唤醒只处理 1024 个事件
        if (static_cast<size_t>(n) == events_.size() && events_.size() < 65536) 
        {
            events_.resize(events_.size() * 2);
            LOG_INFO("EventLoop events_ resized to " + std::to_string(events_.size()));
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

        // 关键：swap 到局部变量，本轮事件处理完后由下一轮开头统一析构
        garbage.swap(pendingConnections_);
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
    assertInLoopThread();

    epoll_event ev{};
    // 始终监听 EPOLLRDHUP，对端关闭写端时能感知
    ev.events = events | EPOLLRDHUP;
    ev.data.fd = ch->getFd();

    int ret = epoll_ctl(efd_, EPOLL_CTL_ADD, ch->getFd(), &ev);
    if (ret < 0)
    {
        int saved_errno = errno;
        LOG_ERROR("epoll_ctl ADD failed, fd=" + std::to_string(ch->getFd())
                  + " errno=" + std::to_string(saved_errno)
                  + " (" + std::string(strerror(saved_errno)) + ")");
        return;
    }

    ch->setEvents(events);   // Channel 内部只记录调用者关心的事件
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
        auto conn = connit->second;

        conn->finishClose();

        pendingConnections_.push_back(conn);

        connections_.erase(connit);
    }
}

void EventLoop::updateChannelEvent(Channel* ch, uint32_t events) 
{
    assertInLoopThread();

    //这个优化有bug
    // 事件没变 → 跳过 epoll_ctl，减少系统调用
    //if (ch->getEvents() == events) 
    //{
    //    return;
    //}

    epoll_event ev{};
    ev.events = events | EPOLLRDHUP;
    ev.data.fd = ch->getFd();

    int ret = epoll_ctl(efd_, EPOLL_CTL_MOD, ch->getFd(), &ev);
    if (ret < 0)
    {
        int saved_errno = errno;
        LOG_ERROR("epoll_ctl MOD failed, fd=" + std::to_string(ch->getFd())
                  + " errno=" + std::to_string(saved_errno)
                  + " (" + std::string(strerror(saved_errno)) + ")");
        return;
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

void EventLoop::setTimer(int interval_sec, std::function<void()> cb)
{
    timerCallback_ = std::move(cb);

    //创建timerfd
    timerfd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd_ < 0)
    {
        LOG_ERROR("timerfd_create failed: " + std::string(strerror(errno)));
        return; 
    }

    //设置周期：首次 interval_sec 后触发，之后每 interval_sec 触发一次
    struct itimerspec its {};
    its.it_value.tv_sec = interval_sec;
    its.it_interval.tv_sec = interval_sec;

    if (::timerfd_settime(timerfd_, 0, &its, nullptr) < 0)
    {
        LOG_ERROR("timerfd_settime failed: " + std::string(strerror(errno)));
        ::close(timerfd_);
        timerfd_ = -1;
        return;
    }

    // 创建 Channel, 注册读事件
    timerChannel_ = std::make_unique<Channel> (timerfd_);
    timerChannel_->setreadCallBack ([this] { handleTimer(); });
    updateChannel (timerChannel_.get(), EPOLLIN);

    LOG_INFO("Timer set to " + std::to_string(interval_sec) + "s interval");

}

void EventLoop::handleTimer()
{
    // 必须读出到期次数，否则 timerfd 一直可读
    // 内核维护,要么读8字节,要不没读
    uint64_t expirations = 0;
    ssize_t n = ::read(timerfd_, &expirations, sizeof(expirations));
    if (n != sizeof(expirations))
    {
        LOG_WARN("timerfd read failed");
        return;
    }

    // 执行用户回调
    if (timerCallback_) {
        timerCallback_();
    }

}

void EventLoop::checkIdleConnections(int timeout_sec)
{
    assertInLoopThread();   // 只在所属线程执行
    
    std::vector<int> timeout_fds;
    timeout_fds.reserve(16);
    
    for (const auto& [fd, conn] : connections_) 
    {
        if (conn->idleSeconds() > timeout_sec) 
        {
            timeout_fds.push_back(fd);
        }
    }
    
    if (timeout_fds.empty()) return;
    
    LOG_INFO("Timeout scan: " + std::to_string(timeout_fds.size()) 
             + " connections idle for more than " 
             + std::to_string(timeout_sec) + "s");
    
    for (int fd : timeout_fds)
    {
        // 关键：不能直接 erase，要走统一关闭流程
        auto it = connections_.find(fd);
        if (it == connections_.end()) continue;
        
        auto conn = it->second;
        // handleClose 是幂等的，内部会触发 closeCallback
        // closeCallback 里会调 onConnectionClosed + removeConnection
        conn->close();   // 线程安全（内部 runInLoop）
    }
}

bool EventLoop::isInLoopThread() const 
{
    return std::this_thread::get_id() == owner_thread_id_;
}