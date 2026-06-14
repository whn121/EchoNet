#include "Net/Socket.h"
#include "Logger/logger.h"
#include "Net/Connection.h"
#include "ThreadPool/ThreadPool.h"
#include <memory>
#include "Reactor/EventLoop.h"
#include "Reactor/Acceptor.h"

#define LISTEN_PORT 8080

int main ()
{
    Socket socket;

    bool ifs = socket.socket_("ipv4", "tcp");
    if (!ifs)
    {
        ERROR("创建失败");
        throw "自己找差距";
    }
    INFO("创建成功");

    socket.setReuseAddr(1);
    socket.setNonBlocking(socket.getFd());

    bool ifb = socket.bind_(LISTEN_PORT);
    if (!ifb)
    {
        ERROR("绑定失败");
        return -1;
    }
    INFO("绑定成功");
    bool ifl= socket.listen_();
    if (!ifl)
    {
        ERROR("监听失败");
        return -1;
    }
    INFO("监听成功");
    INFO("等待链接");

    ThreadPool pool;

    EventLoop loop;

    Acceptor acceptor(socket.getFd(), &loop);
    
    pool.submit_([&loop]{loop.loop();});
    
    std::cin.get();

}