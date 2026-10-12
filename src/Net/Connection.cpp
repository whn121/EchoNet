#include "Net/Connection.h"
#include "Logger/AsyncLogger.h"
#include "Metrics/Metrics.h"
#include <sys/uio.h> //解决buffer一次只能读一块, 减少系统调用


// 构造函数：转移 Channel 和 Protocol 的所有权，保存 EventLoop 指针
Connection::Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop,
                       std::unique_ptr<Protocol> protocol)
    : afd_(afd), loop_(loop), channel_(std::move(channel)), protocol_(std::move(protocol)) {}

Connection::~Connection()
{
    // 如果 finishClose 已经关过，afd_ == -1，避免重复 close
    if (afd_ != -1)
    {
        ::close(afd_);
        afd_ = -1;
    }
}

// ---------- 读事件处理 ----------
void Connection::read()
{
    // 对端EOF后不再读取, 但允许已收到的请求完成处理
    if(state_ != State::Connected || peerReadClosed_)
    {
        return;
    }

    static constexpr size_t EXTRA_BUF_SIZE = 65536;  //用于定义**编译期可确定值、带静态属性的常量**
    char extrabuf[EXTRA_BUF_SIZE] = {};  //64kb

    // 循环读取，直到内核缓冲区没有数据（EAGAIN）或发生错误
    while (true)
    {
        // 保证 Buffer 至少有一些可写空间
        // 如果 writable == 0，先扩容
        size_t writable = inBuffer_.writableBytes();
        if (writable == 0) {
            inBuffer_.enableWrite(EXTRA_BUF_SIZE);
            writable = inBuffer_.writableBytes();
        }

        struct  iovec vec[2];
        vec[0].iov_base = inBuffer_.beginWrite();
        vec[0].iov_len  = writable;
        vec[1].iov_base = extrabuf;
        vec[1].iov_len  = sizeof(extrabuf);

        ssize_t n = readv(afd_, vec, 2);  // 一次性读两块

        if (n > 0)
        {
            const size_t bytes = static_cast<size_t>(n);

            if (bytes <= writable)
            {
                // 数据全在 Buffer 里
                inBuffer_.moveWritePtr(bytes);
            }
            else
            {
                // Buffer 填满，剩余在 extrabuf
                inBuffer_.moveWritePtr(writable);
                inBuffer_.bufferAppend(extrabuf, bytes - writable);
            }

            updateActiveTime();

            Metrics::bytes_read += bytes;
            //继续循环,尝试再读（内核可能有更多数据）
        }
        else if (n == 0)
        {
            // 对端发送方向已关闭，不能立即关闭整个连接。
            // 前面 readv() 已经读取的数据仍然需要经过协议解析。
            peerReadClosed_ = true;

            // 停止读事件关注, 避免半关闭后反复触发读事件
            channel_->disableReading();
            loop_->updateChannelEvent(channel_.get(), channel_->getEvents());

            // 退出读取循环, 继续执行下方解析循环
            break;
        }
        else
        {
            // n < 0,根据errno判断
            if (errno == EINTR) continue; //信号中断,立即重试
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; //内核缓冲区空了, 等待下一次epoll事件
            // 其他错误，关闭连接
            handleClose();
            return;
        }
    }
    // 循环解析所有可解析的完整请求
    while (true)
    {
        ParseResult res = protocol_->parse(inBuffer_);

        if (res == ParseResult::OK)
        {
            // 连接已经进入关闭流程，不再接受新的业务请求
            if (!isConnected())
            {
                break;
            }

            // 解析成功，封装任务投递给业务线程池
            Task task;
            task.conn_ = shared_from_this();          // 延长 Connection 生命周期
            task.message_ = protocol_->getMessage();  // 取出解析好的请求消息

            //  解决work知道协议,解耦不彻底的问题 关键,把业务处理封装
            auto msg = task.message_;
            auto handler = businessHandler_; //复制一份

            std::weak_ptr<Connection> weakself = shared_from_this();

            task.run_ = [weakself, msg, handler]()
            {
                auto self = weakself.lock();
                // 对象仍存活, 不代表链接仍允许启动新的业务处理
                if (!self || !self->isConnected()) return;

                if (handler)
                {
                    handler (self, msg);
                }

            };

            task.onComplete_ = [weakself]()
            {
                if (auto self = weakself.lock())
                {
                    self->onTaskComplete();
                }
            };

            enqueueTask(std::move(task));
            protocol_->reset();  // 重置协议状态，准备解析下一个请求
        }
        else if (res == ParseResult::ERROR)
        {
            // 协议错误：尝试获取协议层预置的错误响应
            auto errResp = protocol_->getErrorResponse();
            if (errResp.has_value())
            {
                sendResponse(errResp.value());   // 发送错误响应给客户端
                protocol_->reset();
            }
            else
            {
                // 协议层没有提供错误响应，直接关闭连接
                handleClose();
            }
            break;
        }
        else
        {
            //NEED_MORE: 普通情况下保留半包, 等待后续数据
            // 但如果对端已经EOF, 就不可能再收到剩余字节
            if (peerReadClosed_ && inBuffer_.getreadable() > 0)
            {
                LOG_WARN("Connection fd=" + std::to_string(afd_) + "peer EOF with incomplete frame; discarding"
                + std::to_string(inBuffer_.getreadable()) + " bytes");

                // 丢弃 EOF 时仍不完整的尾帧。
                // 前面已经成功解析的完整请求不受影响。
                inBuffer_.moveReadPtr(inBuffer_.getreadable());
            }

            break;
        }
    }

    // EOF 后检查业务任务与输出是否全部排空。
    maybeFinishAfterPeerEof();
}

// ---------- 写事件处理 ----------
void Connection::write()
{
    if(state_ != State::Connected)
    {
        return;
    }

    while (outBuffer_.getreadable() > 0)
    {
         //MSG_NOSIGNAL 用于避免向已经断开的对端发送数据时, 因为 SIGPIPE 导致整个进程意外终止
        ssize_t n = send(afd_, outBuffer_.peek(), outBuffer_.getreadable(), MSG_NOSIGNAL);
        if (n > 0)
        {
            outBuffer_.moveReadPtr(n);   // 发送成功，移动读指针

            updateActiveTime();

            Metrics::bytes_written += n;
        }
        else if (n == 0)
        {
            handleClose();
            return;
        }
        else
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 资源暂时不可用，等待下次
            handleClose();
            return;
        }
    }

    // 发送缓冲区已空：取消 EPOLLOUT
    // 关键：先改 Channel 内存状态，再同步给 epoll
    if (outBuffer_.getreadable() == 0)
    {
        channel_->disableWriting();
        loop_->updateChannelEvent(channel_.get(), channel_->getEvents());

        // 如果对端已经EOF, 检查能否完成最终关闭
        maybeFinishAfterPeerEof();
    }
}

// 发送响应, 入队列
void Connection::sendResponse(const std::any& response)
{
    // 回调已经提交, 但可能尚未被Eventloop执行
    // 先计数, 避免EOF收尾误认为没有待发送工作
    pendingSendCallbacks_.fetch_add(1, std::memory_order_relaxed);

    std::weak_ptr<Connection> weakself = shared_from_this();

    loop_->runInLoop( [weakself,response]
    {
        auto self = weakself.lock();
        if(!self) return;

        // 进入回调后, 先结束等待执行计数
        self->pendingSendCallbacks_.fetch_sub(1, std::memory_order_acq_rel);

        if(!self->isConnected()) return;

        std::string data = self->protocol_->encode(response);

        {
            std::lock_guard<std::mutex> lock (self->sendMutex_);

            self->sendQueue_.push (std::move(data));
        }

        self->flushSendQueue();

        // flush 后再次检查。
        // 若已排空，可以完成 EOF 收尾；若仍有数据，检查会返回。
        self->maybeFinishAfterPeerEof();

    });
}

Channel* Connection::getChannel()
{
    return channel_.get();
}

void Connection::setcloseCallback(std::function<void(int)> close)
{
    closeCallBack_ = close;
}

void Connection::myClose()
{
    handleClose();
}

// 初始化：将 Channel 的回调绑定到 Connection 的成员函数
void Connection::init()
{
    //不能活的强引用,要不然循环引用了,无法析构计数不会归零
    std::weak_ptr<Connection> weakself = shared_from_this();   // 获得自身的weak_ptr，防止回调中对象被释放

    channel_->setreadCallBack([weakself] { if (auto self = weakself.lock()) { self -> read(); } });      // 读就绪,lock尝试升级为临时强引用,回调结束析构
    channel_->setwriteCallBack([weakself] { if (auto self = weakself.lock()) { self -> write(); } });    // 写就绪,lock尝试升级为临时强引用,回调结束析构
    channel_->setcloseCallBack([weakself] { if (auto self = weakself.lock()) { self -> myClose(); } });  // 挂起/错误,lock尝试升级为临时强引用,回调结束析构
}

void Connection::setCallBack(std::function<void(Task)> callback)
{
    worksumbitcallback_ = callback;   // 设置业务处理回调
}

void Connection::close()
{
    std::weak_ptr<Connection> weakself = shared_from_this();
    loop_->runInLoop([weakself] {
        auto self = weakself.lock();
        if (self) {
            self->handleClose();   // 在 EventLoop 线程里执行
        }
    });
}

// 返回距离上次活跃经过的秒数
    int64_t Connection::idleSeconds() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - last_active_).count();
    }

void Connection::handleClose()
{
    // CAS：只有一个线程能从 Connected 进入 Closing
    State expected = State::Connected;
    if (!state_.compare_exchange_strong(expected, State::Closing))
    {
        // 已经被别人关过了，或者已经是 Closing/Closed
        return;
    }

    LOG_INFO("handleClose: fd=" + std::to_string(afd_));

    // 优雅关闭写端：告诉对端我们不再发数据
    // 注意：shutdown 不关闭 fd，真正的 close 在 finishClose()
    if (afd_ != -1)
    {
        ::shutdown(afd_, SHUT_WR);
    }

    // 通知 EventLoop / Server 清理
    bool deferFinalClose = false;

    // 清理尚未开始执行的业务任务
    {
        std::lock_guard<std::mutex> lock (taskMutex_);
        while (!pendingTasks_.empty())
        {
            pendingTasks_.pop();
        }

        // taskRunning_ 表示已有任务被调用, 或正在执行
        // 这种情况下, 不能立即清理业务Session/Room
        deferFinalClose = taskRunning_;
        closeCallbackPending_ = deferFinalClose;
    }

    // 清空还在sendQueue_中排队的响应
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        while (!sendQueue_.empty())
        {
            sendQueue_.pop();
        }
    }

    // 释放两个Buffer中仍未消费的逻辑数据
    // 这里只清空缓冲区内容, 不释放底层vector的容量
    inBuffer_.moveReadPtr(inBuffer_.getreadable());
    outBuffer_.moveReadPtr(outBuffer_.getreadable());

    if (deferFinalClose)
    {
        // 先从epoll移除该链接, 避免他继续接受网络事件
        // 暂时保留connections_映射和fd, 等待业务任务完成
        loop_->removeChannel(channel_.get());

        LOG_INFO("handleClose: defer final close until task completion, fd=" + std::to_string(afd_));

        return;
    }

    // 没有待完成任务，沿用原来的关闭回调。
    // IoThreadPool 回调负责业务清理、removeConnection 和 finishClose。
    if (closeCallBack_)
    {
        closeCallBack_(afd_);
    }
}

void Connection::maybeFinishAfterPeerEof()
{
    // 只处理对端发送方向已关闭、但连接尚未关闭的情况。
    // 本函数由所属 EventLoop 线程调用。
    if (!peerReadClosed_ || state_ != State::Connected)
    {
        return;
    }

    // 条件一：没有正在执行或等待中的业务任务。
    bool tasksDrained = false;

    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        tasksDrained = !taskRunning_ && pendingTasks_.empty();
    }

    if (!tasksDrained)
    {
        return;
    }

    // 条件二：没有等待 EventLoop 执行的发送回调。
    if (pendingSendCallbacks_.load(std::memory_order_acquire) != 0)
    {
        return;
    }

    // 条件三：没有仍在 sendQueue_ 中等待合并的消息。
    bool queuedResponsesEmpty = false;

    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        queuedResponsesEmpty = sendQueue_.empty();
    }

    if (!queuedResponsesEmpty)
    {
        return;
    }

    // 条件四：outBuffer_ 中没有尚未发送的字节。
    if (outBuffer_.getreadable() != 0)
    {
        return;
    }

    // 业务与输出均排空，可以进入最终关闭流程。
    handleClose();
}

void Connection::finishClose()
{
    if(state_ == State::Closed)
    {
        return;
    }


    state_ = State::Closed;

    // 真正关闭 fd，避免 pendingConnections_ 延迟析构期间占着 fd
    if (afd_ != -1)
    {
        ::close(afd_);
        afd_ = -1;
    }

    LOG_INFO("finishClose done");
}

void Connection::enqueueTask(Task task)
{
    Task nextTask;
    bool needSubmit = false;

    {
        std::lock_guard<std::mutex> lock(taskMutex_);

        pendingTasks_.push(std::move(task));

        if (!taskRunning_)
        {
            taskRunning_ = true;

            nextTask = std::move(pendingTasks_.front());
            pendingTasks_.pop();

            needSubmit = true;
        }
    }

    if (needSubmit && worksumbitcallback_)
    {
        worksumbitcallback_(std::move(nextTask));
    }
}

void Connection::onTaskComplete()
{
    Task nextTask;
    bool needSubmit = false;
    bool noMoreTasks = false;
    bool needFinalizeClose = false;

    {
        std::lock_guard<std::mutex> lock(taskMutex_);

        //  如果链接已经进入终止关闭流程, 不再调度后续业务任务
        if (state_.load() != State::Connected)
        {
            while (!pendingTasks_.empty())
            {
                pendingTasks_.pop();
            }

            taskRunning_ = false;
            noMoreTasks = true;

            // 认领延迟关闭任务, 确保只安排一次最终关闭
            if (closeCallbackPending_)
            {
                closeCallbackPending_ = false;
                needFinalizeClose = true;
            }
        }

        else if (pendingTasks_.empty())
        {
            // 当前任务是最后一个任务
            taskRunning_ = false;
            noMoreTasks = true;
        }
        else
        {
            // 保持同一链接上的FIFO顺序
            nextTask = std::move(pendingTasks_.front());
            pendingTasks_.pop();

            taskRunning_ = true;
            needSubmit = true;
        }
    }

    // 继续执行下一个业务任务
    if (needSubmit && worksumbitcallback_)
    {
        worksumbitcallback_(std::move(nextTask));
    }
    else if (needFinalizeClose)
    {
        // 工作线程不能直接清理 EventLoop / Channel。
        // 回到所属 EventLoop，执行原有关闭回调。
        std::weak_ptr<Connection> weakself = shared_from_this();

        loop_->runInLoop([weakself]
        {
            if (auto self = weakself.lock())
            {
                if (self->closeCallBack_)
                {
                    // IoThreadPool中的回调会先清理业务层
                    // 再执行removeConnection()和finishClose()
                    self->closeCallBack_(self->afd_);
                }
            }
        });
    }
    else if (noMoreTasks)
    {
        // 普通任务完成后, 检查对端EOF场景是否可以收尾
        std::weak_ptr<Connection> weakself = shared_from_this();

        loop_->runInLoop([weakself]()
        {
            if (auto self = weakself.lock())
            {
                self->maybeFinishAfterPeerEof();
            }
        });
    }
}

void Connection::flushSendQueue()
{
    if (state_ != State::Connected)
    {
        return;
    }

    std::queue<std::string> temp;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        temp.swap(sendQueue_);
    }

    while (!temp.empty())
    {
        auto& msg = temp.front();
        outBuffer_.bufferAppend(msg.data(), msg.size());
        temp.pop();
    }

    // 高水位保护：客户端只读不写，outBuffer_ 会无限增长
    // 超过阈值直接关闭，避免 OOM
    if (outBuffer_.getreadable() > HIGH_WATER_MARK)
    {
        LOG_WARN("Connection fd=" + std::to_string(afd_)
                 + " outBuffer exceeds high water mark: "
                 + std::to_string(outBuffer_.getreadable())
                 + " bytes, closing");
        handleClose();
        return;
    }

    // 有数据待发 → 启用 EPOLLOUT
    if (outBuffer_.getreadable() > 0)
    {
        channel_->enableWriting();
        loop_->updateChannelEvent(channel_.get(), channel_->getEvents());
    }
}