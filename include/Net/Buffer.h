#pragma one
#include "logger.h"

class Buffer 
{
public:
    Buffer();
    ~Buffer();

private:
    std::string buffer;

public:
    void bappend (const char* val, size_t len);
    std::string retrieveMessage();  
    bool hasMessage() const; 
    void clean();
    std::string getbuffer();//connection用
};