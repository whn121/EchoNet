#include "Session/Session.h"

void Session::send(const MyMessage &msg)
{
    connection_ -> sendResponse (msg);
}

void Session::close()
{
    if (connection_) 
    {
        connection_->close();   // 调用 Connection 的线程安全关闭方法
    }
}
