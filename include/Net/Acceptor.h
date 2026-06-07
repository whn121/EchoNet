#pragma one
#include "Net/Socket.h"

class Acceptor
{
public:
    Acceptor();
    ~Acceptor();

private:
    int afd_;

public:
    bool aaccept(MySocket& valms, sockaddr_in& valad);
    int getafd ()const;
};