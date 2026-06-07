#include "Net/Acceptor.h"

bool Acceptor::aaccept(MySocket &valms, sockaddr_in &valad)
{
    afd_ = valms.Accept(valad);
    return afd_ >= 0;
}
Acceptor::Acceptor()
{
    afd_ = -1;
}

Acceptor::~Acceptor() = default;//不可关闭afd会提前关闭

int Acceptor::getafd() const
{
    return afd_;
}