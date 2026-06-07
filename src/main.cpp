#include "logger.h" //日志,方便打印和维护,可惜不能存储
#include "threadpool/ThreadPool.h" //线程池支持多链接,但好像还是阻塞
//#include "Net/Socket.h" //所有要手动释放的都应该封装用raii管理
//#include "Net/Buffer.h" //创建缓冲区,维护tcp数据
#include "Net/Connection.h"//管理缓冲区和裸露的接受,发送
#include "Net/Acceptor.h"
int main()
{
    MySocket Socket;
    Socket.Socket("ipv4", "tcp");
    sockaddr_in addr{}; //(记录地址和接口)
    addr.sin_family = AF_INET; // (方式)
    addr.sin_port = htons(8080); // (接口)
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // (地址)
    Socket.Bind(addr);
    Socket.Listen(128);
    ThreadPool test(4); //初始化线程池
    
    Acceptor accrptor;
    while (true)
    {
        sockaddr_in aaddr{};
        bool ifa = accrptor.aaccept(Socket, aaddr);
        if (!ifa)continue;
        auto conn = std::make_shared<Connection>(accrptor.getafd());//Connecton是在堆上的用智能指针管理conn是在栈上的,但有计数块在.
        test.submit([conn]()
        //如果传引用,传的是栈上内存,connection结束调用析构函数,但是线程池还是使用这个引用,就会选悬空引用,很危险
        {
            while(true)
            {
                bool ifcr = conn -> read();
                if (!ifcr) break; //加入退出要不一直循环
                while(conn -> getbuffer().hasMessage())
                {
                    std::string rev = conn -> getbuffer().retrieveMessage();
                    INFO(rev);
                    conn -> write(rev);
                }
            }
            INFO (std::string("执行完成"));
        });//要传递参数
    }
}

