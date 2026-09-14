#pragma once

#include "Net/Connection.h"
#include "Protocol/MyProtocol.h"
#include <chrono>
#include <memory>

class Session 
{
public:
    Session(std::shared_ptr<Connection> conn, uint32_t user_id, const std::string& username)
    : connection_ (std::move(conn)), user_id_ (user_id), user_name_ (username)
    , logged_in_ (true), last_active_time_ (time(nullptr)){}

    //只读
    uint32_t getUserId() const { return user_id_; };
    const std::string& getUsername() const { return user_name_; }
    bool isLoggedIn() const { return logged_in_.load(); }

    //可变状态
    uint32_t getRoomId() const { return room_id_.load(); }
    void setRoomId(uint32_t id) { room_id_.store(id); }

    time_t getActiveTime() const {return last_active_time_.load();}
    void updateActiveTime() {last_active_time_ = time(nullptr);}

    //发送消息
    void send(const MyMessage& msg);

    //关闭链接
    void close();//请求关闭绘画,心跳超时踢人

private:
    //不能拷贝
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::shared_ptr<Connection> connection_;
    
    //不可变
    const uint32_t user_id_ = 0;
    const std::string user_name_;
    const std::atomic<bool> logged_in_ = false; //是否已经登录

    //可变
    std::atomic<uint32_t> room_id_ = 0;
    std::atomic<time_t> last_active_time_{time(nullptr)}; //最后活跃时间秒

};