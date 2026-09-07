#pragma once

#include "Session/Session.h"
#include <vector>
#include <memory>

class Room
{
public:
    Room(uint32_t id, std::string& name);
    void addMember(std::shared_ptr<Session>);
    void removeMember(std::shared_ptr<Session>);
    void broadcast(const MyMessage& mag); //便利成员列表,调用每个成员的send;
    size_t memberCount() const;//查询成员数量
    void setidname(uint32_t, std::string&);

private:
    uint32_t id_ = 0;
    std::string name_;
    std::vector<std::shared_ptr<Session>> members_;
    mutable std::mutex mutex_; //突破const可以在const函数里修改
};