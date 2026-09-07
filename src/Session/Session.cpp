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

void Session::updateActiveTime()
{
    auto sec = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    last_active_time_ = sec;
}

uint32_t Session::getRoomId() const
{
    return room_id_;
}

time_t Session::getActiveTime() const
{
    return last_active_time_;
}

std::string Session::getUsername() const
{
    return user_name_;
}
