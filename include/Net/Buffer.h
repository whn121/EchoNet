#pragma once
#include <string>

class Buffer
{
public:
    Buffer();
    ~Buffer();

private:
    std::string buffer_;
    
public:
    void append(const char* msg, size_t len);
    bool getMessage(std::string buf);
    bool hasMessage()const;
    void clean();
    void writeBuffer(const char* msg, size_t len);

};