#pragma once

#include "Net/Connection.h"
#include "Protocol/MyProtocol.h"
#include <chrono>
#include <memory>

class Session 
{
public:
    Session(std::shared_ptr<Connection> conn);
    void setUser(uint32_t tuser_id, std::string& username); //登录后调用设置
    void send(const MyMessage& msg);
    void setRoomId(uint32_t room_id);
    uint32_t getRoomId() const;
    void updateActiveTime() {last_active_time_ = time(nullptr);}
    time_t getActiveTime() const {return last_active_time_.load();}
    std::string getUsername() const;
    uint32_t getUserId() const;
    void close();//请求关闭绘画,心跳超时踢人

private:
    std::shared_ptr<Connection> connection_;
    uint32_t user_id_ = 0;
    std::string user_name_;
    uint32_t room_id_ = 0;
    bool logged_in_ = false; //是否已经登录
    std::atomic<time_t> last_active_time_{time(nullptr)}; //最后活跃时间秒

};