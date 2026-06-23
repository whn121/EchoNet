#include "Net/Socket.h"
#include "Logger/logger.h"
#include "Net/Connection.h"
#include <memory>
#include "Server/Server.h"

int main ()
{
    Server server;
    server.start ();
}