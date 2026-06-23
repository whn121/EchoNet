#include "Net/Buffer.h"

Buffer::Buffer() : read_ptr_ (0), write_ptr_ (0)
{
    setSize (4096);
}

Buffer::~Buffer() = default;

void Buffer::bufferAppend (const char* msg, size_t len)
{
    enableWrite(len);
    memcpy(buffer_.data() + write_ptr_, msg, len);
    write_ptr_ += len;
}

void Buffer::setSize (size_t size)
{
    buffer_.resize (size);
}

size_t Buffer::getreadptr () const
{
    return read_ptr_;
}

size_t Buffer::getwriteptr () const
{
    return write_ptr_;
}

const char *Buffer::peek()
{
    return buffer_.data() + read_ptr_;
}

void Buffer::enableWrite(size_t len)
{
    size_t writeabble = buffer_.size() - write_ptr_;
    size_t readable = write_ptr_ - read_ptr_;
    if (writeabble < len)
    {
        if (writeabble + read_ptr_ >= len)
        {
            memmove(buffer_.data(), buffer_.data() + read_ptr_, readable);
            read_ptr_ = 0;
            write_ptr_ = readable;
        }
        while (buffer_.size() - write_ptr_ < len)
        {
            setSize (buffer_.size() + 4096);
        }
    }
}

size_t Buffer::getreadable () const
{
    return write_ptr_ - read_ptr_;
}

void Buffer::goReadPtr (size_t len)
{
    read_ptr_ += len;
}

void Buffer::goWritePtr (size_t len)
{
    write_ptr_ += len;
}