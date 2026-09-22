#pragma once

#include <vector>
#include <string>
#include <cstring>

// 自动扩容的读写缓冲区（类似环形缓冲区，但通过整理数据实现）
class Buffer 
{
public:
    Buffer();
    ~Buffer();
    void bufferAppend(const char* msg, size_t len);  // 追加数据
    void setSize(size_t size);
    size_t getreadptr() const;
    size_t getwriteptr() const;
    const char* peek();                              // 返回可读数据起始指针
    void enableWrite(size_t len);                    // 确保有足够写空间
    size_t getreadable() const;                      // 可读字节数
    void moveReadPtr(size_t len);                      // 移动读指针（消费数据）
    void moveWritePtr(size_t len);                     // 移动写指针

    //实现readv的函数 一次系统调用读入多块(我读两块)块内存
    size_t writableBytes() const { return buffer_.size() - write_ptr_; } //可写字节数
    char* beginWrite() { return buffer_.data() + write_ptr_; } //可写起始指针


private:
    std::vector<char> buffer_;   // 底层存储
    size_t read_ptr_ = 0;        // 读位置
    size_t write_ptr_ = 0;       // 写位置
};