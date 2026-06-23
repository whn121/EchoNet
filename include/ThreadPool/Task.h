#pragma once
#include <memory>
#include "HTTP/HttpContext.h"  

class Connection;  // 前向声明

struct Task {
    std::shared_ptr<Connection> conn_;
    HttpRequest req_;
};