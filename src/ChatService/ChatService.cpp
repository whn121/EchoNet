#include "ChatService/ChatService.h"
#include <sstream>
#include "Logger/AsyncLogger.h"
#include "Metrics/Metrics.h"


ChatService::ChatService()
{
    mysqlPool_ = std::make_unique<MySQLPool>("127.0.0.1", "root", "2788138053", "echonet", 4);
    redisPool_ = std::make_unique<RedisPool>("127.0.0.1", 6379, 4);

    // 从数据库恢复 next_room_id_
    initIdsFromDatabase();

    // 启动异步 DB 写入线程
    db_writer_thread_ = std::thread([this] { dbWriterLoop(); });
}

ChatService::~ChatService()
{
    // 通知写入线程停止
    {
        std::lock_guard<std::mutex> lock(msg_queue_mutex_);
        db_writer_stop_ = true;
    }
    msg_queue_cv_.notify_all();

    // 等它处理完队列剩余消息后退出
    if (db_writer_thread_.joinable()) {
        db_writer_thread_.join();
    }
}

ChatService& ChatService::instance()
{
    static ChatService service;
    return service;
}

void ChatService::handleMessage (std::shared_ptr<Connection> conn, const MyMessage& msg)
{
    Metrics::total_requests++;   // 每条消息 +1

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
            Metrics::error_requests++;

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
    // 从 MySQL 验证
    bool valid = false;
    auto mysqlConn = mysqlPool_->getConnection();
    if (!mysqlConn)
    {
        LOG_ERROR("Failed to get MySQL connection");
        MyMessage res;
        res.type_ = MyType::LOGIN_RESP;
        res.id_ = msg.id_;
        res.payload_ = "sql conecction fail";
        conn->sendResponse(res);
        return;
    }

    MYSQL* raw = mysqlConn.get();

    //使用预处理先构建语法树,再把数据当做参数传递,避免直接传拼接的会有sql注入的风险
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt)
    {
        LOG_ERROR("stmt_init failed");
        // 返回 FAIL 响应
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    const char* sql = "SELECT password FROM users WHERE username=?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
        LOG_ERROR("mysql_stmt_prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    //绑定参数
    MYSQL_BIND bind[1];
    memset (bind, 0, sizeof(bind));
    unsigned long username_len = username.length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)username.c_str();
    bind[0].buffer_length = username_len;
    bind[0].length = &username_len;

    if (mysql_stmt_bind_param(stmt, bind)) {
        LOG_ERROR("mysql_stmt_bind_param: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    if (mysql_stmt_execute(stmt)) {
        LOG_ERROR("mysql_stmt_execute: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    // 绑定结果
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    char db_password[256] = {0};
    unsigned long db_password_len = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = db_password;
    result_bind[0].buffer_length = sizeof(db_password);
    result_bind[0].length = &db_password_len;

    if (mysql_stmt_bind_result(stmt, result_bind)) {
        LOG_ERROR("mysql_stmt_bind_result: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    int fetch_ret = mysql_stmt_fetch(stmt);
    if (fetch_ret == 0 && userpassword == std::string(db_password, db_password_len)) {
        valid = true;
    }

    mysql_stmt_close(stmt);

    if (!valid) 
    {
        MyMessage resp;
        resp.type_ = MyType::LOGIN_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }
    // 分配用户ID
    uint32_t user_id = next_session_id_++;

    // 创建 Session
    auto session = std::make_shared<Session>(conn, user_id, username);

    //存入表
    int fd = conn->getChannel()->getFd();
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[fd] = session;
    }

    auto redisConn = redisPool_->getConnection();
    
    if (redisConn) 
    {
        redisContext* redis = redisConn.get();
        redisReply* reply = (redisReply*)redisCommand(redis, "SET user:%d:online 1", user_id);
        if (reply == nullptr) 
        {
            LOG_ERROR("Redis SET online failed: " + std::string(redis->errstr));
        } 
        else 
        {
            freeReplyObject(reply);
        }
    }
    else 
    {
        LOG_WARN("Redis unavailable, skip SET online");
    }

    MyMessage resp;
    resp.type_ = MyType::LOGIN_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK";
    conn->sendResponse(resp);

    Metrics::total_connections++;
    Metrics::active_connections++;

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

    // 插入 rooms 表
    auto mysqlConn = mysqlPool_->getConnection();
    
    if (!mysqlConn)
    {
        LOG_ERROR("Failed to get MySQL connection");
        MyMessage res;
        res.type_ = MyType::CREATE_ROOM_RESP;
        res.id_ = msg.id_;
        res.payload_ = "FAIL";
        conn->sendResponse(res);
        return;
    }

    MYSQL* raw = mysqlConn.get();
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    
    if (!stmt)
    {
        LOG_ERROR("mysql_stmt_init failed");
        MyMessage resp;
        resp.type_ = MyType::CREATE_ROOM_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    const char* sql = "INSERT INTO rooms (id, room_name) VALUES (?, ?)";
   
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
        LOG_ERROR("mysql_stmt_prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::CREATE_ROOM_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    // 绑定两个参数
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    // 第一个参数：room_id（整数）
    uint32_t room_id_val = room_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &room_id_val;
    bind[0].buffer_length = sizeof(room_id_val);

    // 第二个参数：room_name（字符串）
    unsigned long name_len = room_name.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)room_name.c_str();
    bind[1].buffer_length = name_len;
    bind[1].length = &name_len;

    if (mysql_stmt_bind_param(stmt, bind))
    {
        LOG_ERROR("mysql_stmt_bind_param: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::CREATE_ROOM_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    if (mysql_stmt_execute(stmt))
    {
        LOG_ERROR("mysql_stmt_execute: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        MyMessage resp;
        resp.type_ = MyType::CREATE_ROOM_RESP;
        resp.id_ = msg.id_;
        resp.payload_ = "FAIL";
        conn->sendResponse(resp);
        return;
    }

    mysql_stmt_close(stmt);

    // 创建 Room 对象，存入内存 rooms_
    auto room = std::make_shared<Room> (room_id, room_name);
    {
        std::lock_guard<std::mutex> lock (rooms_mutex_);
        rooms_[room_id] = room;
    }

    // 创建者自动加入房间
    room->addMember(session);
    session->setRoomId(room_id);

    // 返回房间ID和人数：格式 "OK|room_id|1"
    MyMessage resp;
    resp.type_ = MyType::CREATE_ROOM_RESP;
    resp.id_ = msg.id_;
    resp.payload_ = "OK|" + std::to_string(room_id) + "|1";
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
    broadcastMemberCount(room_id);//人数更新

    auto redisConn = redisPool_->getConnection();
    
    if (redisConn) 
    {
        redisContext* redis = redisConn.get();
        redisReply* reply = (redisReply*)redisCommand(redis, "SADD room:%d:members %d", room_id, session->getUserId());
        if (reply == nullptr) 
        {
            LOG_ERROR("Redis SADD room member failed: " + std::string(redis->errstr));
        } 
        else 
        {
            freeReplyObject(reply);
        }
    }
    else 
    {
        LOG_WARN("Redis unavailable, skip SADD room member");
    }

    MyMessage resp;
    resp.type_ = MyType::JOIN_ROOM_RESP;
    resp.id_ = msg.id_;
    size_t count = target_room->memberCount();
    resp.payload_ = "OK|" + std::to_string(count);
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

    broadcastMemberCount(room_id);//人数更新

    // 检查并清理空房间
    checkAndRemoveEmptyRoom(room_id);

    auto redisConn = redisPool_->getConnection();
    
    if (redisConn) 
    {
        redisContext* redis = redisConn.get();
        redisReply* reply = (redisReply*)redisCommand(redis, "SREM room:%d:members %d", room_id, session->getUserId());
        if (reply == nullptr) 
        {
            LOG_ERROR("Redis SREM room member failed: " + std::string(redis->errstr));
        } 
        else 
        {
            freeReplyObject(reply);
        }
    }
    else 
    {
        LOG_WARN("Redis unavailable, skip SREM room member");
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

    // 异步持久化：入队，不阻塞
    enqueueMessage(room_id, session->getUserId(), msg.payload_);

}

void ChatService::handleHearbeat(std::shared_ptr<Connection> conn, const MyMessage & msg)
{
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

void ChatService::checkAndRemoveEmptyRoom(uint32_t room_id)
{
    // 第一步：从内存移除
    bool removed_from_memory = false;
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto it = rooms_.find(room_id);
        if (it != rooms_.end() && it->second->memberCount() == 0)
        {
            rooms_.erase(it);
            removed_from_memory = true;
            LOG_INFO("Room " + std::to_string(room_id) + " removed from memory");
        }
    }

    if (!removed_from_memory) return;   // 非空，不处理

    // 第二步：从 MySQL 删除
    auto mysqlConn = mysqlPool_->getConnection();
    if (!mysqlConn)
    {
        LOG_WARN("MySQL unavailable, cannot delete room " + std::to_string(room_id));
        return;
    }

    MYSQL* raw = mysqlConn.get();
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt)
    {
        LOG_ERROR("mysql_stmt_init failed");
        return;
    }

    const char* sql = "DELETE FROM rooms WHERE id = ?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
        LOG_ERROR("mysql_stmt_prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    uint32_t rid = room_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &rid;
    bind[0].buffer_length = sizeof(rid);

    if (mysql_stmt_bind_param(stmt, bind))
    {
        LOG_ERROR("mysql_stmt_bind_param: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt))
    {
        LOG_ERROR("mysql_stmt_execute: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return;
    }

    mysql_stmt_close(stmt);
    LOG_INFO("Room " + std::to_string(room_id) + " removed from MySQL");
}

void ChatService::initIdsFromDatabase()
{
    auto mysqlConn = mysqlPool_->getConnection();
    if (!mysqlConn)
    {
        LOG_WARN("MySQL unavailable at startup, next_room_id_ starts from 1");
        return;
    }

    MYSQL* raw = mysqlConn.get();

    if (mysql_query(raw, "SELECT IFNULL(MAX(id), 0) FROM rooms") != 0)
    {
        LOG_ERROR("Failed to query MAX(id) from rooms");
        return;
    }

    MYSQL_RES* res = mysql_store_result(raw);
    if (!res)
    {
        LOG_ERROR("mysql_store_result failed");
        return;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && row[0])
    {
        uint32_t max_id = static_cast<uint32_t>(std::stoul(row[0]));
        next_room_id_ = max_id + 1;
        LOG_INFO("next_room_id_ initialized to " + std::to_string(next_room_id_.load()));
    }

    mysql_free_result(res);
}

void ChatService::dbWriterLoop()
{
    std::vector<MessageRecord> batch;
    batch.reserve(BATCH_MAX_SIZE);

    while (true)
    {
        std::unique_lock<std::mutex> lock(msg_queue_mutex_);

        // 等待：队列非空或停止
        msg_queue_cv_.wait_for(lock, std::chrono::milliseconds(BATCH_INTERVAL_MS),
                               [this] { return !msg_queue_.empty() || db_writer_stop_; });

        // 取一批（最多 BATCH_MAX_SIZE 条）
        while (!msg_queue_.empty() && batch.size() < BATCH_MAX_SIZE) 
        {
            batch.push_back(std::move(msg_queue_.front()));
            msg_queue_.pop();
        }

        // 判断是否可以退出
        bool should_exit = db_writer_stop_ && msg_queue_.empty() && batch.empty();

        lock.unlock();

        if (!batch.empty()) 
        {
            batchInsertMessages(batch);
            batch.clear();
        }

        if (should_exit) break;
    }

    LOG_INFO("DB writer thread exit, dropped_messages=" + std::to_string(dropped_messages_.load()));
}

bool ChatService::batchInsertMessages(std::vector<MessageRecord> &batch)
{
    if (batch.empty()) return true;

    auto mysqlConn = mysqlPool_->getConnection();
    if (!mysqlConn) 
    {
        LOG_ERROR("MySQL unavailable, dropping " + std::to_string(batch.size()) + " messages");
        return false;
    }

    MYSQL* raw = mysqlConn.get();

    // 构造批量 INSERT SQL
    std::string sql = "INSERT INTO messages (room_id, user_id, content) VALUES ";
    for (size_t i = 0; i < batch.size(); ++i) 
    {
        if (i > 0) sql += ",";
        sql += "(?,?,?)";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) 
    {
        LOG_ERROR("mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) 
    {
        LOG_ERROR("prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定 N*3 个参数
    size_t n = batch.size();
    std::vector<MYSQL_BIND> binds(n * 3);
    std::memset(binds.data(), 0, binds.size() * sizeof(MYSQL_BIND));

    // 中间变量必须在 execute 前一直有效
    std::vector<uint32_t>      room_ids(n);
    std::vector<uint32_t>      user_ids(n);
    std::vector<unsigned long> content_lens(n);

    for (size_t i = 0; i < n; ++i) 
    {
        room_ids[i] = batch[i].room_id;
        user_ids[i] = batch[i].user_id;
        content_lens[i] = batch[i].content.size();

        binds[i*3+0].buffer_type   = MYSQL_TYPE_LONG;
        binds[i*3+0].buffer        = &room_ids[i];
        binds[i*3+0].buffer_length = sizeof(uint32_t);

        binds[i*3+1].buffer_type   = MYSQL_TYPE_LONG;
        binds[i*3+1].buffer        = &user_ids[i];
        binds[i*3+1].buffer_length = sizeof(uint32_t);

        binds[i*3+2].buffer_type   = MYSQL_TYPE_STRING;
        binds[i*3+2].buffer        = (void*)batch[i].content.data();
        binds[i*3+2].buffer_length = content_lens[i];
        binds[i*3+2].length        = &content_lens[i];
    }

    if (mysql_stmt_bind_param(stmt, binds.data())) 
    {
        LOG_ERROR("bind_param: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) 
    {
        LOG_ERROR("execute: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}

void ChatService::enqueueMessage(uint32_t room_id, uint32_t user_id, const std::string &content)
{
    {
        std::lock_guard<std::mutex> lock(msg_queue_mutex_);
        if (msg_queue_.size() >= MAX_QUEUE_SIZE) 
        {
            dropped_messages_++;
            LOG_WARN("Message queue full (" + std::to_string(msg_queue_.size()) + "), dropping message");
            return;
        }
        msg_queue_.push({room_id, user_id, content});
    }
    msg_queue_cv_.notify_one();
}

void ChatService::onConnectionClosed(int fd) 
{
    LOG_INFO("onConnectionClosed: fd=" + std::to_string(fd));

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

    // 先保存房间ID，后面广播用
    uint32_t leftRoomId = 0;

    if (session)
    {
        Metrics::active_connections--;   // ← 登录过的连接关闭就减

        // 清理 Redis 在线状态
        auto redisConn = redisPool_->getConnection();
       
        if (redisConn) 
        {
            redisContext* redis = redisConn.get();
            redisReply* reply = (redisReply*)redisCommand(redis, "DEL user:%d:online", session->getUserId());
            if (reply == nullptr) 
            {
                LOG_ERROR("Redis DEL online failed: " + std::string(redis->errstr));
            } 
            else 
            {   
                freeReplyObject(reply);
            }
        }
        else 
        {
            LOG_WARN("Redis unavailable, skip DEL online");
        }

        // 如果用户在房间中，则离开房间
        if (session->getRoomId() != 0) 
        {
            leftRoomId = session->getRoomId();

            // 在锁内移除成员
            {
                std::lock_guard<std::mutex> lock(rooms_mutex_);
                auto it = rooms_.find(leftRoomId);
                if (it != rooms_.end()) 
                {
                    it->second->removeMember(session);
                }
            }

            // 锁已经释放，在这里广播人数变化
            broadcastMemberCount(leftRoomId);

            checkAndRemoveEmptyRoom(leftRoomId);
        }
    }
}

void ChatService::printMetrics()
{
    LOG_INFO("===== Metrics =====");
    LOG_INFO("total_connections: " + std::to_string(Metrics::total_connections.load()));
    LOG_INFO("active_connections: " + std::to_string(Metrics::active_connections.load()));
    LOG_INFO("total_requests: " + std::to_string(Metrics::total_requests.load()));
    LOG_INFO("error_requests: " + std::to_string(Metrics::error_requests.load()));
    LOG_INFO("bytes_read: " + std::to_string(Metrics::bytes_read.load()));
    LOG_INFO("bytes_written: " + std::to_string(Metrics::bytes_written.load()));
}

void ChatService::broadcastMemberCount(uint32_t room_id)
{
    // 获取房间 shared_ptr（内部加锁）
    std::shared_ptr<Room> room = getRoomById(room_id);
    if (!room) return;

    // 构造人数更新消息
    MyMessage updateMsg;
    updateMsg.type_ = MyType::MEMBER_COUNT_UPDATE;
    updateMsg.id_ = 0;   // 主动推送无请求ID
    updateMsg.payload_ = std::to_string(room_id) + "|" + std::to_string(room->memberCount());

    // 广播给房间内所有成员（包括刚加入/离开的成员，客户端会校验）
    room->broadcast(updateMsg);
}

std::shared_ptr<Room> ChatService::getRoomById(uint32_t room_id)
{
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) return it->second;
    return nullptr;
}