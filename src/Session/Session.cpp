#include "Session/Session.h"

Session::Session(std::shared_ptr<Connection> conn) : connection_ (conn)
{   
}

void Session::setUser(uint32_t tuser_id, std::string &username)
{
    user_id_ = tuser_id;
    user_name_ = username;
    logged_in_ = true;
}

void Session::send(const MyMessage &msg)
{
    connection_ -> sendResponse (msg);
}

void Session::setRoomId(uint32_t room_id)
{
    room_id_ = room_id;
}

uint32_t Session::getRoomId() const
{
    return room_id_;
}

std::string Session::getUsername() const
{
    return user_name_;
}

uint32_t Session::getUserId() const
{
    return user_id_;
}

void Session::close()
{
    if (connection_) 
    {
        connection_->close();   // 调用 Connection 的线程安全关闭方法
    }
}
