#include "Net/Buffer.h"

Buffer::Buffer() : read_ptr_(0), write_ptr_(0) {
    setSize(4096);   // 初始容量4KB
}
Buffer::~Buffer() = default;

void Buffer::bufferAppend(const char* msg, size_t len) {
    enableWrite(len);                              // 确保有足够空间
    memcpy(buffer_.data() + write_ptr_, msg, len); // 拷贝数据
    write_ptr_ += len;                             // 移动写指针
}

void Buffer::setSize(size_t size) { buffer_.resize(size); }
size_t Buffer::getreadptr() const { return read_ptr_; }
size_t Buffer::getwriteptr() const { return write_ptr_; }
const char* Buffer::peek() { return buffer_.data() + read_ptr_; }

void Buffer::enableWrite(size_t len) {
    size_t writeable = buffer_.size() - write_ptr_;    // 剩余可写空间
    size_t readable = write_ptr_ - read_ptr_;          // 未消费的数据
    if (writeable < len) {
        // 如果前部有空间（read_ptr_ > 0），将数据移动到头部
        if (writeable + read_ptr_ >= len) {
            memmove(buffer_.data(), buffer_.data() + read_ptr_, readable);
            read_ptr_ = 0;
            write_ptr_ = readable;
        } else {
            // 扩容，每次增加4096字节
            while (buffer_.size() - write_ptr_ < len) setSize(buffer_.size() + 4096);
        }
    }
}

size_t Buffer::getreadable() const { return write_ptr_ - read_ptr_; }
void Buffer::goReadPtr(size_t len) { read_ptr_ += len; }
void Buffer::goWritePtr(size_t len) { write_ptr_ += len; }