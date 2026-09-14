#pragma once

#include "Room/Room.h"
#include "Session/Session.h"
#include "Net/Connection.h"
#include "DB/MySQLPool.h"
#include "DB/RedisPool.h"
#include "Metrics/Metrics.h"


class ChatService
{
public:
    ChatService();
    ~ChatService();

    static ChatService& instance();  //单例入口

    void handleMessage (std::shared_ptr<Connection> conn, const MyMessage& msg); //入口,根据类型分发
    void onConnectionClosed (int fd); //链接关闭时清理

    void printMetrics();//记录观测

private:
    ChatService(const ChatService&) = delete;
    ChatService& operator= (const ChatService&) = delete;

    void handleLogin(std::shared_ptr<Connection> conn, const MyMessage& msg); //登录
    void handleCreateRoom(std::shared_ptr<Connection>, const MyMessage&); //创建方间
    void handleJionRoom(std::shared_ptr<Connection>, const MyMessage&); //加入方将
    void handleLeaveRoom(std::shared_ptr<Connection>, const MyMessage&); //离开房间
    void handleSendMessage(std::shared_ptr<Connection>, const MyMessage&); //发消息
    void handleHearbeat(std::shared_ptr<Connection>, const MyMessage&); //心跳
    
    std::thread heartbeat_thread_; //监听线程
    std::atomic<bool> heartbeat_stop_{false};

    
    void heartbeatLoop(); //监听心跳

    static constexpr int HEARTBEAT_TIMEOUT_SEC = 60;         // 60秒无心跳则踢
    static constexpr int HEARTBEAT_CHECK_INTERVAL_SEC = 30;  // 每30秒检查一次

    void broadcastMemberCount(uint32_t room_id); //回拨人数给客户端
    std::shared_ptr<Room> getRoomById(uint32_t room_id);

    std::shared_ptr<Session> getSessionByConn(std::shared_ptr<Connection> conn);
    std::shared_ptr<Session> getSessionByFd(int fd);

    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::unordered_map<int, std::shared_ptr<Room>> rooms_;
    std::mutex sessions_mutex_;
    std::mutex rooms_mutex_;

    std::atomic<uint32_t> next_session_id_ {1};
    std::atomic<uint32_t> next_room_id_ {1};

    std::unique_ptr<MySQLPool> mysqlPool_;
    std::unique_ptr<RedisPool> redisPool_;

    void checkAndRemoveEmptyRoom(uint32_t room_id); //辅助空房间清理

    void initIdsFromDatabase(); //启东市回复id

};