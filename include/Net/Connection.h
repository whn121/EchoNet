#pragma one
#include "Net/Buffer.h"

class Connection
{
public:
    Connection(int afd);
    ~Connection();

private:
    int afd_;
    Buffer buffer_;

public:
    bool read();
    void write(const std::string& msg);
    Buffer& getbuffer();
};