#include "Common/Config.h"
#include "Logger/logger.h"
#include "Server/Server.h"

int main (int argc, char* argv[])
{
    Config::instance ().parseArgs(argc, argv);

    Server server;
    if (!server.start ())
    {
        INFO ("服务器没启动,自己找差距");
        return 1;
    }

    server.stop ();
    INFO ("服务器安全退出");
    return 0;

}