cmake_minimum_required(VERSION 3.10)

project(EchoNet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(EchoNet
    src/main.cpp
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

)

target_link_libraries(EchoNet PRIVATE mysqlclient hiredis pthread)

target_include_directories(EchoNet PRIVATE ${PROJECT_SOURCE_DIR}/include)