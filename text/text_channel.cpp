#include "Reactor/Channel.h"
#include <cassert>
#include <iostream>
#include <functional>

int main() {
    int dummy_fd = 123; // 测试用假 fd，不会真实使用

    // 测试1：读事件触发读回调
    {
        Channel ch(dummy_fd);
        bool read_called = false;
        ch.setreadCallBack([&read_called] { read_called = true; });
        ch.handleEvent(EPOLLIN);
        assert(read_called == true);
    }

    // 测试2：写事件触发写回调
    {
        Channel ch(dummy_fd);
        bool write_called = false;
        ch.setwriteCallBack([&write_called] { write_called = true; });
        ch.handleEvent(EPOLLOUT);
        assert(write_called == true);
    }

    // 测试3：EPOLLERR 触发关闭回调，且不触发读写回调
    {
        Channel ch(dummy_fd);
        bool read_called = false, write_called = false, close_called = false;
        ch.setreadCallBack([&read_called] { read_called = true; });
        ch.setwriteCallBack([&write_called] { write_called = true; });
        ch.setcloseCallBack([&close_called] { close_called = true; });

        ch.handleEvent(EPOLLERR);
        assert(close_called == true);
        assert(read_called == false);
        assert(write_called == false);
    }

    // 测试4：EPOLLHUP 触发关闭回调
    {
        Channel ch(dummy_fd);
        bool close_called = false;
        ch.setcloseCallBack([&close_called] { close_called = true; });
        ch.handleEvent(EPOLLHUP);
        assert(close_called == true);
    }

    // 测试5：EPOLLRDHUP 触发关闭回调
    {
        Channel ch(dummy_fd);
        bool close_called = false;
        ch.setcloseCallBack([&close_called] { close_called = true; });
        ch.handleEvent(EPOLLRDHUP);
        assert(close_called == true);
    }

    // 测试6：事件状态管理
    {
        Channel ch(dummy_fd);
        assert(ch.getEvents() == 0); // 初始无事件

        ch.enableReading();
        assert(ch.isReading() == true);
        assert((ch.getEvents() & EPOLLIN) != 0);

        ch.enableWriting();
        assert(ch.isWriting() == true);
        assert((ch.getEvents() & EPOLLOUT) != 0);

        ch.disableWriting();
        assert(ch.isWriting() == false);
        assert((ch.getEvents() & EPOLLOUT) == 0);

        ch.disableAll();
        assert(ch.getEvents() == 0);
        assert(ch.isReading() == false);
        assert(ch.isWriting() == false);
    }

    std::cout << "All Channel tests passed!" << std::endl;
    return 0;
}