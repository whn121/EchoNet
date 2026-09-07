#include "Reactor/EventLoop.h"
#include "Reactor/Channel.h"
#include "Net/Connection.h"
#include "Protocol/Protocol.h"
#include "Net/Buffer.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <any>
#include <optional>

// 简单 Echo 协议：收到什么就原样返回
class EchoProtocol : public Protocol {
public:
    ParseResult parse(Buffer& buffer) override {
        if (buffer.getreadable() == 0) return ParseResult::NEED_MORE;
        // 假设一个完整消息就是当前所有数据（实际可自定义边界）
        // 这里简单地把所有可读数据作为一条消息
        std::string msg(buffer.peek(), buffer.getreadable());
        buffer.goReadPtr(buffer.getreadable());   // 消费全部
        message_ = msg;
        return ParseResult::OK;
    }

    std::string encode(const std::any& message) override {
        return std::any_cast<std::string>(message);
    }

    std::any getMessage() override {
        return message_;
    }

    void reset() override {
        message_.clear();
    }

    std::optional<std::any> getErrorResponse() override {
        return std::nullopt;
    }

private:
    std::string message_;
};

int main() {
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    // 设置服务端 fd 非阻塞
    int flags = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);

    EventLoop loop;
    std::thread loop_thread([&loop] { loop.loop(); });

    // 创建 Connection
    auto channel = std::make_unique<Channel>(fds[1]);
    auto protocol = std::make_unique<EchoProtocol>();
    auto conn = std::make_shared<Connection>(fds[1], std::move(channel), &loop, std::move(protocol));

    // 关闭回调：从 EventLoop 移除连接
    conn->setcloseCallback([&loop](int fd) {
        loop.removeConnection(fd);
    });

    // 业务回调：模拟工作线程，直接处理并返回
    conn->setCallBack([](Task task) {
        // 在真实项目中，这里会投递给工作线程池，我们直接处理
        // 任务里的 message_ 是 any 包装的字符串
        std::string request = std::any_cast<std::string>(task.message_);
        // 生成响应：原样返回
        task.conn_->sendResponse(request);
    });

    // 初始化并注册到 EventLoop
    conn->init();
    loop.runInLoop([&loop, conn]() {
        loop.updateChannel(conn->getChannel(), EPOLLIN);
        loop.updateConnection(conn);
    });

    // 客户端发送数据
    std::string test_msg = "Hello, EchoNet!";
    write(fds[0], test_msg.data(), test_msg.size());

    // 等待服务端处理并写回
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 客户端读取响应
    char buf[256] = {};
    ssize_t n = read(fds[0], buf, sizeof(buf));
    assert(n == (ssize_t)test_msg.size());
    assert(memcmp(buf, test_msg.data(), n) == 0);

    // 客户端关闭连接
    close(fds[0]);

    // 等待服务端处理关闭事件
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 停止循环
    loop.stop();
    loop_thread.join();

    // 此时 Connection 应该已经被移除，EventLoop 中无连接
    std::cout << "All Connection tests passed!" << std::endl;
    return 0;
}