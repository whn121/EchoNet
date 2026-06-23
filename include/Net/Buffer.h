#pragma once
#include <vector>
#include <string>
#include <cstring>

class Buffer
{
public:
    Buffer();
    ~Buffer();

private:
    std::vector<char> buffer_;
    size_t read_ptr_;
    size_t write_ptr_;

public:
    void bufferAppend(const char* msg, size_t len);
    void setSize(size_t size);
    size_t getreadptr()const;
    size_t getwriteptr()const;
    const char* peek();
    void enableWrite(size_t len);
    size_t getreadable()const; 
    void goWritePtr (size_t len);
    void goReadPtr (size_t len);

};