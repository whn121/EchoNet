#include "Net/Buffer.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>

int main() 
{
    // 测试1：空Buffer
    {
        Buffer buf;
        assert(buf.getreadable() == 0);
    }

    // 测试2：小量append
    {
        Buffer buf;
        buf.bufferAppend("hello", 5);
        assert(buf.getreadable() == 5);
        assert(memcmp(buf.peek(), "hello", 5) == 0);
    }

    // 测试3：扩容
    {
        Buffer buf;
        std::string data(10000, 'x');
        buf.bufferAppend(data.data(), data.size());
        assert(buf.getreadable() == 10000);
    }

    // 测试4：消费部分
    {
        Buffer buf;
        buf.bufferAppend("hello", 5);
        buf.goReadPtr(3);
        assert(buf.getreadable() == 2);
        assert(memcmp(buf.peek(), "lo", 2) == 0);
    }

    // 测试5：消费后append触发compact
    {
        Buffer buf;
        buf.bufferAppend("hello", 5);
        buf.goReadPtr(5);
        buf.bufferAppend("world", 5);
        assert(buf.getreadable() == 5);
        assert(memcmp(buf.peek(), "world", 5) == 0);
    }

    // 测试6：边界（这个在Debug下会失败，用于验证assert）
    // 注意：这个测试正常应该注释掉，只在你想验证assert时打开
    // {
    //     Buffer buf;
    //     buf.bufferAppend("hello", 5);
    //     buf.goReadPtr(6);  // 应该触发 assert
    // }

    std::cout << "All Buffer tests passed!" << std::endl;
    return 0;
}