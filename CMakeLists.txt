cmake_minimum_required(VERSION 3.10)

project(EchoNet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(EchoNet
    src/main.cpp
    src/logger.cpp
)

target_include_directories(EchoNet PRIVATE ${PROJECT_SOURCE_DIR}/include)
