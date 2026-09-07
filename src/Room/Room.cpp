#include "Room/Room.h"
#include <algorithm> //find()

Room::Room(uint32_t id, std::string& name) : id_ (id), name_(name)
{
}

void Room::addMember(std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock (mutex_);
    auto it = std::find(members_.begin(), members_.end(), session);
    if (it == members_.end()) 
    {
        members_.push_back(session);
    }
}

void Room::removeMember(std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock (mutex_);
    for (auto it = members_.begin(); it != members_.end(); )
    {
        if (*it == session)
        {
            it = members_.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void Room::broadcast(const MyMessage &mag)
{
    std::vector<std::shared_ptr<Session>> members_copy;
    {
        std::lock_guard<std::mutex> lock (mutex_);
        members_copy = members_; 
    }
    for (auto it = members_.begin(); it != members_.end(); ++it)
    {
        (*it)->send (mag); //要加括号哭死 
    }
}

size_t Room::memberCount() const
{
    return members_.size();
}

void Room::setidname(uint32_t id, std::string & name)
{
    id_ = id;
    name_ = name;
}
