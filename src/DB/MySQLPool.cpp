#include "DB/MySQLPool.h"
#include "Logger/AsyncLogger.h"


MySQLPool::MySQLPool(const std::string& host, const std::string& user, const std::string& password, 
const std::string& db, int pool_size) :host_ (host), user_ (user), password_(password), db_ (db), pool_size_ (pool_size)
{
    for (int i = 0; i < pool_size_; i++)
    {
        MYSQL* conn = mysql_init (nullptr);
        if (!conn)
        {
            LOG_ERROR("mysql_init failed");
            continue;
        }
        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), password_.c_str(), db_.c_str(), 0, nullptr, 0))
        {
            LOG_ERROR("mysql_real_connect failed: " + std::string(mysql_error(conn)));
            mysql_close(conn);
            continue;
        }
        pool_.push(conn);
    }

}


MySQLPool::~MySQLPool()
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) 
    {
        MYSQL* conn = pool_.front();
        pool_.pop();
        mysql_close(conn);
    }
}

std::shared_ptr<MYSQL> MySQLPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    //降级,修复阻塞无超时问题
    bool ok = cv_.wait_for(lock, std::chrono::milliseconds(500), [this] { return !pool_.empty(); });
    if (!ok) 
    {
        LOG_ERROR("MySQLPool getConnection timeout");
        return nullptr;
    }

    MYSQL* conn = pool_.front();
    pool_.pop();
    lock.unlock();   // 解锁再做健康检查

    // 健康检查
    if (mysql_ping(conn) != 0) {
        LOG_WARN("MySQL connection dead, reconnecting...");
        mysql_close(conn);

        conn = mysql_init(nullptr);
        if (!conn || !mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                        password_.c_str(), db_.c_str(), 0, nullptr, 0)) {
            LOG_ERROR("MySQL reconnect failed");
            if (conn) mysql_close(conn);
            return nullptr;
        }
    }

    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) { releaseConnection(c); });
}

//用完不删除返回队列复用
void MySQLPool::releaseConnection(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}