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
    if(state_ != State::Connected)
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
            //对端正常关闭
            handleClose();
            return;
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
                if (!self) return;

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
            //NEED_MORE
            break;
        }
    }
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
        ssize_t n = send(afd_, outBuffer_.peek(), outBuffer_.getreadable(), 0);
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
    }
}

// 发送响应, 入队列
void Connection::sendResponse(const std::any& response)
{
    std::weak_ptr<Connection> weakself = shared_from_this();

    loop_->runInLoop( [weakself,response]
    {
        auto self = weakself.lock();

        if(!self) return;

        if(!self->isConnected()) return;

        std::string data = self->protocol_->encode(response);

        {
            std::lock_guard<std::mutex> lock (self->sendMutex_);

            self->sendQueue_.push (std::move(data));
        }

        self->flushSendQueue();

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
    // 注意：shutdown 不关闭 fd，真正的 close 在 finishClose
    if (afd_ != -1)
    {
        ::shutdown(afd_, SHUT_WR);
    }

    // 通知 EventLoop / Server 清理
    if (closeCallBack_) 
    {
        closeCallBack_(afd_);
    }

    // 清空未执行的业务任务
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        while (!pendingTasks_.empty()) 
        {
            pendingTasks_.pop();
        }
    }

    // 清空未发送的数据，防止 socket 关了还占内存
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        while (!sendQueue_.empty()) 
        {
            sendQueue_.pop();
        }
    }
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

    {
        std::lock_guard<std::mutex> lock(taskMutex_);

        if (pendingTasks_.empty())
        {
            // 没有后续任务
            taskRunning_ = false;
            return;
        }

        // FIFO：一定取队头
        nextTask = std::move(pendingTasks_.front());
        pendingTasks_.pop();

        // 继续保持 running 状态
        taskRunning_ = true;

        needSubmit = true;
    }

    if (needSubmit && worksumbitcallback_)
    {
        worksumbitcallback_(std::move(nextTask));
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