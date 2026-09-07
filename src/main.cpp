#include "Common/Config.h"
#include "Logger/logger.h"
#include "Server/Server.h"
#include "Protocol/HttpProtocol.h"
#include <memory>
#include "Net/Connection.h"
#include "Protocol/MyProtocol.h"

int main (int argc, char* argv[])
{
    Config::instance ().parseArgs(argc, argv);

    Server server;
    server.setcreator ([](int afd, EventLoop* loop) -> std::shared_ptr<Connection>
    {
        auto channel = std::make_unique<Channel>(afd);
        auto protocol = std::make_unique<MyProtocol>();
        auto conn = std::make_shared<Connection>(afd, std::move(channel), loop, std::move(protocol));

        return conn;
    });

    if (!server.start ())
    {
        LOG_INFO ("服务器没启动,自己找差距");
        return 1;
    }

    server.stop ();
    LOG_INFO ("服务器安全退出");
    return 0;

}