#pragma once

#include <mysql/mysql.h>
#include <mutex>
#include <memory>
#include <string>
#include <queue>
#include <condition_variable>


class MySQLPool
{
public:
    MySQLPool(const std::string& host, const std::string& user, const std::string& password, const std::string& db, int pool_size = 4);
    ~MySQLPool();

    std::shared_ptr<MYSQL> getConnection();
    void releaseConnection(MYSQL* conn);

private:
    std::queue<MYSQL*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string host_, user_, password_, db_;
    int pool_size_;

};