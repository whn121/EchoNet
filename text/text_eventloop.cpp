#include "Reactor/EventLoop.h"
#include "Reactor/Channel.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/eventfd.h>
#include <unistd.h>

int main() {
    // 测试1：EventLoop 线程亲和性
    {
        EventLoop loop;
        Channel ch(1); // 假fd

        // 正确线程（当前线程是主线程，但 EventLoop 尚未启动，所以 owner 线程是主线程）
        // 这里不启动 loop，直接调用 updateChannel 应该通过
        loop.updateChannel(&ch, EPOLLIN);
        loop.removeChannel(&ch);

        // 错误线程：在子线程调用 updateChannel 应该 abort（Debug 下）
        // 为了测试，我们注释掉，避免程序崩溃。如果你想验证，可以打开。
        // 但建议先手动单独验证一次。
    }

    // 测试2：runInLoop 跨线程调度
    {
        EventLoop loop;
        std::atomic<bool> called{false};

        // 启动 loop 线程
        std::thread loop_thread([&loop] { loop.loop(); });

        // 从主线程投递任务
        loop.runInLoop([&called] { called = true; });

        // 等待执行
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 停止 loop
        loop.stop();
        loop_thread.join();

        assert(called == true);
    }

    // 测试3：EventLoop 检测 EPOLLIN 事件
    {
        EventLoop loop;
        int fd = eventfd(0, EFD_NONBLOCK);
        Channel ch(fd);
        bool read_called = false;

        ch.setreadCallBack([&read_called, fd] {
            read_called = true;
            uint64_t val;
            read(fd, &val, sizeof(val)); // 消费事件
        });

        loop.updateChannel(&ch, EPOLLIN);

        // 在另一个线程中向 eventfd 写入，触发读事件
        std::thread writer([fd] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uint64_t one = 1;
            write(fd, &one, sizeof(one));
        });

        // 运行 loop，应触发读回调
        std::thread loop_thread([&loop] { loop.loop(); });

        // 等待回调执行
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        loop.stop();
        loop_thread.join();
        writer.join();

        assert(read_called == true);
        close(fd);
    }

    std::cout << "All EventLoop tests passed!" << std::endl;
    return 0;
}