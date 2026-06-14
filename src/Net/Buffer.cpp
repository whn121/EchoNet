#include "Net/Buffer.h"

Buffer::Buffer() = default;

Buffer::~Buffer()
{
    clean ();
}

void Buffer::append (const char* msg, size_t len)
{
    buffer_.append(msg, len);
}

bool Buffer::hasMessage () const
{
    size_t fin = buffer_.find('\n');
    if (fin == std::string::npos)
    {
        return false;
    }
    return true;
}

bool Buffer::getMessage (std::string buf)
{
    size_t fin = buffer_.find('\n');
    if (fin == std::string::npos)
    return false;
    buf = buffer_.substr(0, fin + 1);
    buffer_.erase(0, fin + 1);
    return true;
}

void Buffer::clean ()
{
    buffer_.clear();
    buffer_.clear();
}

void Buffer::writeBuffer(const char* msg, size_t len)
{
    buffer_.append(msg, len);
}