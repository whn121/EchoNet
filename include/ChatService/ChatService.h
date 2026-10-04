#pragma once

#include "Room/Room.h"
#include "Session/Session.h"
#include "Net/Connection.h"
#include "DB/MySQLPool.h"
#include "DB/RedisPool.h"
#include "Metrics/Metrics.h"
#include <queue>
#include <condition_variable>
#include "RateLimit/RateLimiter.h"


class ChatService
{
public:
    static ChatService& instance();  //单例入口
    ~ChatService();

    void handleMessage (std::shared_ptr<Connection> conn, const MyMessage& msg); //入口,根据类型分发
    void onConnectionClosed (int fd); //链接关闭时清理

    void printMetrics();//记录观测

    void sendRateLimited(std::shared_ptr<Connection> conn, const MyMessage& msg);

private:
    ChatService();

    ChatService(const ChatService&) = delete;
    ChatService& operator= (const ChatService&) = delete;

    void handleLogin(std::shared_ptr<Connection> conn, const MyMessage& msg); //登录
    void handleCreateRoom(std::shared_ptr<Connection>, const MyMessage&); //创建方间
    void handleJionRoom(std::shared_ptr<Connection>, const MyMessage&); //加入方将
    void handleLeaveRoom(std::shared_ptr<Connection>, const MyMessage&); //离开房间
    void handleSendMessage(std::shared_ptr<Connection>, const MyMessage&); //发消息
    void handleHearbeat(std::shared_ptr<Connection>, const MyMessage&); //心跳

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

    void initIdsFromDatabase(); //启动时回复id

    // 异步 DB 写入
    struct MessageRecord 
    {
        uint32_t room_id;
        uint32_t user_id;
        std::string content;
    };

    std::queue<MessageRecord> msg_queue_;
    std::mutex msg_queue_mutex_;    
    std::condition_variable msg_queue_cv_;
    std::thread db_writer_thread_;
    std::atomic<bool> db_writer_stop_{false};
    std::atomic<uint64_t> dropped_messages_{0};

    static constexpr size_t MAX_QUEUE_SIZE = 500000;   // 队列上限，防 OOM
    static constexpr int    BATCH_INTERVAL_MS = 100;   // 批量刷盘间隔
    static constexpr size_t BATCH_MAX_SIZE = 2000;      // 单批最大条数

    void dbWriterLoop();
    bool batchInsertMessages(std::vector<MessageRecord>& batch);
    void enqueueMessage(uint32_t room_id, uint32_t user_id, const std::string& content);

    std::shared_ptr<RateLimiter> global_limiter_;  // 全局限流
    std::shared_ptr<RateLimiter> user_limiter_;    // 用户级限流

    //分布式服务注册
    std::string service_name_ = "chat_service";
    std::string instance_id_;
    std::string host_ = "127.0.0.1";
    uint16_t    port_ = 0;

    void registerToRedis();

    // 后端心跳续租
    std::thread       heartbeat_thread_;
    std::atomic<bool> heartbeat_stop_{false};

    void heartbeatLoop();

};