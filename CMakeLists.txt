cmake_minimum_required(VERSION 3.10)

project(EchoNet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════ 公共核心库（不含 main） ═══════
set(SRC_CORE
    src/Net/Socket.cpp
    src/Net/Connection.cpp
    src/Net/Buffer.cpp
    src/ThreadPool/TaskQueue.cpp
    src/ThreadPool/WorkThreadPool.cpp
    src/ThreadPool/IoThreadPool.cpp
    src/Reactor/Channel.cpp
    src/Reactor/EventLoop.cpp
    src/Reactor/Acceptor.cpp
    src/HTTP/HttpContext.cpp
    src/HTTP/HttpService.cpp
    src/Server/Server.cpp
    src/Common/Config.cpp
    src/Protocol/HttpProtocol.cpp
    src/Room/Room.cpp
    src/Session/Session.cpp
    src/ChatService/ChatService.cpp
    src/Protocol/MyProtocol.cpp
    src/DB/MySQLPool.cpp
    src/DB/RedisPool.cpp
    src/Logger/AsyncLogger.cpp
    src/Metrics/Metrics.cpp
    src/RateLimit/RedisTokenBucket.cpp
    src/RateLimit/TokenBucket.cpp
    src/gateway_main.cpp
    src/Gateway/ServiceRegistry.cpp
)

add_library(echonet_core STATIC ${SRC_CORE})
target_include_directories(echonet_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(echonet_core PUBLIC mysqlclient hiredis pthread)

# ═══════ EchoNet 后端可执行文件 ═══════
add_executable(EchoNet src/main.cpp)
target_link_libraries(EchoNet PRIVATE echonet_core)

# ═══════ EchoNetGateway 可执行文件 ═══════
add_executable(EchoNetGateway src/gateway_main.cpp)
target_link_libraries(EchoNetGateway PRIVATE echonet_core)