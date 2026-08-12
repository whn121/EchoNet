#include "Reactor/EventLoop.h"
#include "Net/Connection.h"
#include <sys/eventfd.h>
#include <unistd.h>

EventLoop::EventLoop() {
    efd_ = epoll_create1(0);  // 创建epoll实例
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); // 创建eventfd
    events_.resize(1024);

    // 将wakeup_fd_注册到epoll，监听读事件
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd_;
    epoll_ctl(efd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);
}

EventLoop::~EventLoop() {
    if (efd_) close(efd_);
    if (wakeup_fd_) close(wakeup_fd_);
}

void EventLoop::loop() {
    while (!stop_) {
        // 阻塞等待事件，-1表示无限等待
        int n = epoll_wait(efd_, events_.data(), events_.size(), -1);
        for (int i = 0; i < n; ++i) {
            int fd = events_[i].data.fd;
            if (fd == wakeup_fd_) {
                // 唤醒事件：读出数据清空计数器，然后执行投递的任务
                uint64_t dummy;
                read(wakeup_fd_, &dummy, sizeof(dummy));
                std::vector<std::function<void()>> tasks;
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    tasks.swap(task_queue_);
                }
                for (auto& task : tasks) task();
            } else {
                auto it = channels_.find(fd);
                if (it != channels_.end()) {
                    it->second->setRevents(events_[i].events);
                    it->second->callBack(events_[i].events); // 执行回调
                }
            }
        }
    }
}

void EventLoop::runInLoop(std::function<void()> cb) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        task_queue_.emplace_back(std::move(cb));
    }
    wakeUp();  // 唤醒epoll，让其立即处理任务
}

void EventLoop::wakeUp() {
    uint64_t one = 1;
    write(wakeup_fd_, &one, sizeof(one));
}

void EventLoop::stop() {
    stop_ = true;
    wakeUp();  // 唤醒，让循环退出
}

void EventLoop::updateChannel(Channel* ch, uint32_t events) {
    ch->setEvents(events);
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = ch->getFd();
    epoll_ctl(efd_, EPOLL_CTL_ADD, ch->getFd(), &ev);
    channels_[ch->getFd()] = ch;
}

void EventLoop::removeChannel(Channel* ch) {
    epoll_ctl(efd_, EPOLL_CTL_DEL, ch->getFd(), nullptr);
    channels_.erase(ch->getFd());
}

void EventLoop::updateConnection(std::shared_ptr<Connection> conn) {
    connections_[conn->getChannel()->getFd()] = conn;
}

void EventLoop::removeConnection(int fd) {
    epoll_ctl(efd_, EPOLL_CTL_DEL, fd, nullptr);
    channels_.erase(fd);
    connections_.erase(fd);
}

void EventLoop::updateChannelEvent(Channel* ch, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = ch->getFd();
    epoll_ctl(efd_, EPOLL_CTL_MOD, ch->getFd(), &ev);
}