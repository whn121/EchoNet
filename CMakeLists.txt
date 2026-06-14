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
    src/ThreadPool/ThreadPool.cpp 
    src/Reactor/Channel.cpp 
    src/Reactor/EventLoop.cpp 
    src/Reactor/Acceptor.cpp 
)

target_include_directories(EchoNet PRIVATE ${PROJECT_SOURCE_DIR}/include)