
#include "Net/Connection.h"
#include "Protocol/MyProtocol.h"
#include "ThreadPool/WorkThreadPool.h"
#include "Reactor/EventLoop.h"
#include "Reactor/Channel.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>

int main()
{
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
        perror("socketpair");
        return 2;
    }

    const int flags = ::fcntl(fds[0], F_GETFL, 0);
    if (flags < 0 ||
        ::fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("fcntl");
        ::close(fds[0]);
        ::close(fds[1]);
        return 2;
    }

    WorkThreadPool workers(1);
    EventLoop loop;

    std::promise<void> handled;
    std::promise<void> closed;
    auto handledFuture = handled.get_future();
    auto closedFuture = closed.get_future();

    std::atomic<int> handledCount{0};
    std::atomic<int> closedCount{0};

    auto conn = std::make_shared<Connection>(
        fds[0],
        std::make_unique<Channel>(fds[0]),
        &loop,
        std::make_unique<MyProtocol>());

    conn->init();

    conn->setCallBack(
        [&workers](Task task)
        {
            workers.submit(std::move(task));
        });

    conn->setBusinessHandler(
        [&](std::shared_ptr<Connection> connection,
            const std::any& anyMessage)
        {
            const auto request = std::any_cast<MyMessage>(anyMessage);

            ++handledCount;
            handled.set_value();

            // 模拟对端完全关闭后，业务层仍尝试响应。
            connection->sendResponse(
                std::any{MyMessage{
                    MyType::HEARTBEAT_RESP, request.id_, "ok"
                }});
        });

    conn->setcloseCallback(
        [&](int fd)
        {
            ++closedCount;
            loop.removeConnection(fd);
            closed.set_value();
        });

    loop.updateChannel(conn->getChannel(), EPOLLIN);
    loop.updateConnection(conn);

    const auto request =
        MyProtocol::encodeMessage(MyType::HEARTBEAT_REQ, 321, "");

    if (::send(fds[1], request.data(), request.size(), 0)
        != static_cast<ssize_t>(request.size()))
    {
        perror("send");
        ::close(fds[0]);
        ::close(fds[1]);
        return 3;
    }

    // 在 EventLoop 开始处理前关闭对端，制造数据与 HUP 同时就绪的场景。
    ::close(fds[1]);
    fds[1] = -1;

    std::thread loopThread([&loop] { loop.loop(); });

    const bool handledOk =
        handledFuture.wait_for(std::chrono::seconds(2))
        == std::future_status::ready;

    const bool closedOk =
        closedFuture.wait_for(std::chrono::seconds(2))
        == std::future_status::ready;

    loop.stop();
    if (loopThread.joinable())
    {
        loopThread.join();
    }

    workers.stop();

    if (fds[1] != -1)
    {
        ::close(fds[1]);
    }

    std::cout << "handled=" << handledCount.load()
              << " closed=" << closedCount.load() << '\n';

    if (!handledOk || !closedOk ||
        handledCount.load() != 1 ||
        closedCount.load() != 1)
    {
        std::cerr
            << "FAIL: buffered request lost during HUP\n";
        return 1;
    }

    std::cout << "PASS: buffered request processed before close\n";
    return 0;
}
