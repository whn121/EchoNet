#pragma once

#include "Net/Buffer.h"
#include <unistd.h>
#include <sys/socket.h>
#include "Reactor/Channel.h"
#include <memory>
#include "Reactor/EventLoop.h"
#include "ThreadPool/Task.h"
#include "Protocol/Protocol.h"      // 只依赖抽象协议，不依赖 HTTP

// 表示一个 TCP 连接，管理读写缓冲区、协议解析、回调投递
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(int afd, std::unique_ptr<Channel> channel, EventLoop* loop,
               std::unique_ptr<Protocol> protocol);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void read();                                          // 处理读事件
    void write();                                         // 处理写事件
    void sendResponse(const std::any& response);          // 发送响应（完全解耦协议）
    Channel* getChannel();                                // 返回内部的 Channel 指针
    void setcloseCallback(std::function<void(int)> close);// 设置连接关闭回调
    void myClose();                                       // 主动关闭连接
    void init();                                          // 初始化回调绑定
    void setCallBack(std::function<void(Task)>);          // 设置业务处理回调

private:
    int afd_;                                  // socket 文件描述符
    EventLoop* loop_;                          // 所属的事件循环（该连接所有事件在此 loop 中处理）
    Buffer inBuffer_;                          // 输入缓冲区
    Buffer outBuffer_;                         // 输出缓冲区
    std::unique_ptr<Channel> channel_;         // 对应的 Channel
    std::function<void(int)> closeCallBack_;   // 关闭回调（通知 EventLoop 清理）
    std::unique_ptr<Protocol> protocol_;       // 协议对象（多态）
    std::function<void(Task)> worksumbitcallback_; // 将任务投递给工作线程池的回调
};