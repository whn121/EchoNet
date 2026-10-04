#include "Common/Config.h"
#include "Logger/AsyncLogger.h"
#include "Server/Server.h"
#include "Protocol/HttpProtocol.h"
#include <memory>
#include "Net/Connection.h"
#include "Protocol/MyProtocol.h"
#include "ChatService/ChatService.h"


int main (int argc, char* argv[])
{
    Config::instance ().parseArgs(argc, argv);
    ChatService::instance();

    Server server;
    server.setcreator ([](int afd, EventLoop* loop) -> std::shared_ptr<Connection>
    {
        auto channel = std::make_unique<Channel>(afd);
        auto protocol = std::make_unique<MyProtocol>();
        auto conn = std::make_shared<Connection>(afd, std::move(channel), loop, std::move(protocol));

        return conn;
    });

    // 注入业务处理器：只有 Application 层知道 MyMessage 和 ChatService
    server.setBusinessHandler(
        [](std::shared_ptr<Connection> conn, const std::any& message)
        {
            try
            {
                auto msg = std::any_cast<MyMessage>(message);
                ChatService::instance().handleMessage(conn, msg);
            }
            catch (const std::bad_any_cast& e)
            {
                LOG_ERROR("bad_any_cast in business handler: " + std::string(e.what()));
            }
        }
    );

    // 注入连接关闭回调
    server.setCloseCallback(
        [](int fd)
        {
            ChatService::instance().onConnectionClosed(fd);
        }
    );

    if (!server.start ())
    {
        LOG_INFO ("服务器没启动,自己找差距");
        return 1;
    }

    server.stop ();
    ChatService::instance().printMetrics();
    LOG_INFO ("服务器安全退出");

    AsyncLogger::instance().stop();
    
    return 0;

}