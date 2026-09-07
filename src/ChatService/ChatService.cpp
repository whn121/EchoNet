#include "ChatService/ChatService.h"
#include <sstream>
#include "Logger/logger.h"


ChatService& ChatService::instance()
{
    static ChatService service;
    return service;
}

void ChatService::handleMessage (std::shared_ptr<Connection> conn, const MyMessage& msg)
{
    switch (msg.type_)
    {
    case MyType::LOGIN_REQ:
        handleLogin(conn, msg);
        break;
    case MyType::CREATE_ROOM_REQ:
        handleCreateRoom(conn, msg);
        break;
    case MyType::JOIN_ROOM_REQ:
        handleJionRoom(conn, msg);
        break;
    case MyType::LEAVE_ROOM_REQ:
        handleLeaveRoom(conn, msg);
        break;
    case MyType::SEND_MSG_REQ:
        handleSendMessage(conn, msg);
        break;
    case MyType::HEARTBEAT_REQ:
        handleHearbeat(conn, msg);
        break;
    default:
        // 未知类型，返回错误
        {
            MyMessage resp;
            resp.type_ = MyType::ERROR_RESP;
            resp.id_ = msg.id_;
            resp.payload_ = "Unsupported message type";
            conn->sendResponse(resp);
        }
        break;
    }
}

void ChatService::handleLogin(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    // payload 格式: "username|password"
    std::string payload = msg.payload_;
    auto pos = payload.find ('|');

    if (pos == std::string::npos)
    {
        MyMessage res;
        res.type_ = MyType::LOGIN_RESP;
        res.id_ = msg.id_;
        res.payload_ = "Invalid login payload";
        conn->sendResponse(res);
        return;
    }
    std::string username = payload.substr (0, pos);
    std::string userpassword = payload.substr (pos + 1);
    //验证密码
    if (!(username == "whn" && userpassword == "278813" || username == "whnzs" && userpassword == "278813"))
    {
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";

        conn->sendResponse (resp);
        return;
    }
    // 分配用户ID
    uint32_t user_id = next_session_id_++;

    // 创建 Session
    auto session = std::make_shared<Session>(conn);
    session->setUser(user_id, username);

    //存入表
    int fd = conn->getChannel()->getFd();
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[fd] = session;
    }

    MyMessage resp;
    resp.type_ = MyType::LOGIN_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK";
    conn->sendResponse(resp);

    LOG_INFO("User " + username + " logged in, fd=" + std::to_string(fd));//先用可能影响速度
}

void ChatService::handleCreateRoom(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    auto session = getSessionByConn (conn);
    if (!session) 
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Not logged in";
        conn->sendResponse(resp);
        return;
    }

    std::string room_name = msg.payload_;
    if (room_name.empty()) 
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Room name empty";
        conn->sendResponse(resp);
        return;
    }

    uint32_t room_id = next_room_id_++;
    auto room = std::make_shared<Room> (room_id, room_name);
    {
        std::lock_guard<std::mutex> lock (rooms_mutex_);
        rooms_[room_id] = room;
    }

    MyMessage resp;
    resp.type_ = MyType::CREATE_ROOM_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = std::to_string(room_id);
    conn->sendResponse(resp);

}

void ChatService::handleJionRoom(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    auto session = getSessionByConn (conn);
    if (!session)
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Not logged in";
        conn->sendResponse(resp);
        return;
    }
    uint32_t room_id = std::stoul (msg.payload_);
    
    //获得新房间
    std::shared_ptr<Room> target_room;
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto it = rooms_.find (room_id);
        if (it == rooms_.end())
        {
            MyMessage resp;
            resp.type_ = MyType::ERROR_RESP;
            resp.id_ = msg.id_;
            resp.payload_ = "Room not found";
            conn->sendResponse(resp);
            return;
        }
        target_room = it -> second;
    }

    //如果已经在其他房间，先离开旧房间
    uint32_t old_room_id = session -> getRoomId();
    if (old_room_id != 0)
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto its = rooms_.find (old_room_id);
        if (its != rooms_.end())
        {
            its -> second -> removeMember (session);
        }
    }

    // 加入新房间
    target_room->addMember(session);
    session->setRoomId(room_id);

    MyMessage resp;
    resp.type_ = MyType::JOIN_ROOM_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK";
    conn->sendResponse(resp);

}

void ChatService::handleLeaveRoom(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    auto session = getSessionByConn (conn);
    if (!session)
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Not logged in";
        conn->sendResponse(resp);
        return;
    }

    uint32_t room_id = session->getRoomId();
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto it = rooms_.find(room_id);
        if (it != rooms_.end())
        {
            it -> second -> removeMember(session);
        }
        session -> setRoomId (0);
    }


    MyMessage resp;
    resp.type_ = MyType::LEAVE_ROOM_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK";
    conn->sendResponse(resp);

}

void ChatService::handleSendMessage(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    auto session = getSessionByConn(conn);
    if (!session) 
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Not logged in";
        conn->sendResponse(resp);
        return;
    }
    uint32_t room_id = session->getRoomId();
    if (room_id ==0)
    {
        MyMessage resp;
        resp.type_ = MyType::ERROR_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "Not in any room";
        conn->sendResponse(resp);
        return;
    }

    // 获取房间
    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto it = rooms_.find(room_id);
        if (it == rooms_.end()) 
        {
            MyMessage resp;
            resp.type_ = MyType::ERROR_RESP;
            resp.id_ = msg.id_;
            resp.payload_ = "Room not found";
            conn->sendResponse(resp);
            return;
        }
        room = it->second;
    }

    // 构造广播消息
    MyMessage broadcast;
    broadcast.type_ = MyType::BROADCAST_MSG;
    broadcast.id_ = 0;   // 服务器主动推送，可不带请求ID
    broadcast.payload_ = std::to_string(room_id) + "|" + session->getUsername() + "|" + msg.payload_;

    room->broadcast(broadcast);

    // 给发送者确认
    MyMessage resp;
    resp.type_ = MyType::SEND_MSG_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK";
    conn->sendResponse(resp);

}

void ChatService::handleHearbeat(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
    auto session = getSessionByConn(conn);
    if (session) 
    {
        session->updateActiveTime();
    }

    MyMessage resp;
    resp.type_ = MyType::HEARTBEAT_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "";
    conn->sendResponse(resp);
}


std::shared_ptr<Session> ChatService::getSessionByConn(std::shared_ptr<Connection> conn) 
{
    if (!conn) return nullptr;
    int fd = conn->getChannel()->getFd();
    return getSessionByFd(fd);
}

std::shared_ptr<Session> ChatService::getSessionByFd(int fd) 
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(fd);
    if (it != sessions_.end()) 
    {
        return it->second;
    }
    return nullptr;
}

void ChatService::onConnectionClosed(int fd) 
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(fd);
        if (it != sessions_.end()) 
        {
            session = it->second;
            sessions_.erase(it);
        }
    }

    if (session && session->getRoomId() != 0) 
    {
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(session->getRoomId());
            if (it != rooms_.end()) 
            {
                it -> second -> removeMember(session);
            }
        }
    }
}