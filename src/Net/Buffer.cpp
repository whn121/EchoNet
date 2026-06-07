#include "Net/Buffer.h"

Buffer::Buffer() = default;
Buffer::~Buffer()
{
    clean();
}

void Buffer::bappend(const char* val, size_t len)
{
    buffer.append(val, len);
}

std::string Buffer::retrieveMessage()
{
    size_t finlen = buffer.find('\n'); //定义规则
    if (finlen == std::string::npos) //没找到返回这个
    {
        INFO(std::string("没有找到完整的消息"));
        return "";
    }
    else
    {
        std::string val = buffer.substr(0,finlen + 1);
        buffer = buffer.substr(finlen + 1);
        return val;
    }
}

bool Buffer::hasMessage() const
{
    return buffer.find('\n') != std::string::npos; //没找到返回这个
}

void Buffer::clean()
{
    buffer = "";
}
std::string Buffer::getbuffer()
{
    return buffer;
}