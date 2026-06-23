#include "Net/Connection.h"
#include "Logger/logger.h"

Connection::Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop)
    : afd_(afd), loop_(loop), channel_(std::move(channel))
{}

Connection::~Connection()
{
    if (afd_ > 0)
    close(afd_);
}

void Connection::read()
{
    char buf[1024] = {};
    int n = recv(afd_, buf, sizeof(buf), 0);
    if (n <= 0)
    {
        if (closeCallBack_)
        closeCallBack_(afd_);
        return;
    }

    inBuffer_.bufferAppend (buf, n);

    while(true)
    {
        bool ifok = false;
        size_t n = httpcontext_.parse (inBuffer_.peek(), inBuffer_.getreadable(), httprequest_, ifok);
        if (n == 0) 
        {
            break; // 数据不够，等待更多数据
        }
        inBuffer_.goReadPtr (n);
        if (ifok)
        {
            Task task;
            task.conn_ = move (shared_from_this());
            task.req_ = httprequest_;
            if (worksumbitcallback_) worksumbitcallback_ (task);
            httprequest_ = HttpRequest();
        }
    }
}

void Connection::write ()
{
    while (outBuffer_.getreadable() > 0)
    {
        size_t n = send (afd_, outBuffer_.peek(), outBuffer_.getreadable(), 0);
        if (n == 0)
        {
            if (closeCallBack_) closeCallBack_(afd_);
            return;
        }
        if (n > 0)
        {
            outBuffer_.goReadPtr(n);
        }
        else if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            break;                // 发不出去，等下次
            if (closeCallBack_) closeCallBack_(afd_);
            return ;
        }
    }

    if (outBuffer_.getreadable() == 0)
    {
        loop_->updateChannelEvent(channel_.get(), EPOLLIN);
    }
}


void Connection::setResponse(const HttpResponse& httpresponse)
{
    INFO("正在将回应提压入输出缓冲区");
    std::string version;
    switch (httpresponse.version_)
    {
    case Version::HTTP10:
    version = "HTTP/1.0";//必须带/符合格式
    break;
    case Version::HTTP11:
    version = "HTTP/1.1";
    break;
    default:
    version = "UNKNOWN";
    break;
    }
    std::string buf = "";
    buf += version + " " + std::to_string (httpresponse.status_code_) + " " + 
    httpresponse.status_msg_ + "\r\n";
    for (auto it : httpresponse.header_)
    {
        buf += it.first + ":" + it.second + "\r\n";
    }
    buf += "\r\n" + httpresponse.body_;

    outBuffer_.bufferAppend(buf.data(), buf.size());

    INFO("将数据压入输出缓冲区");

    // 让 epoll 同时监听这个 fd 的可读和可写事件
    loop_ -> runInLoop ([this]
    {loop_->updateChannelEvent(channel_.get(), EPOLLIN | EPOLLOUT);});

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
    closeCallBack_(afd_);
}


void Connection::init()
{
    auto self = shared_from_this();

    channel_->setreadCallBack([self] {
        self->read();
    });

    channel_->setwriteCallBack([self] {
        self->write();
    });

    channel_->setcloseCallBack([self] {
        self->myClose();
    });
}

void Connection::setCallBack (std::function<void(Task)> callback)
{
    worksumbitcallback_ = callback;
}

HttpResponse Connection::handle (HttpRequest req)
{
    HttpResponse reqs = httpservice_.handle (req);
    return reqs;
}