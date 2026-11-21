cmake_minimum_required(VERSION 3.20)
project(android_os LANGUAGES CXX)

add_library(android_os STATIC
    Handler.cpp
    Handler.h
    Looper.cpp
    Looper.h
    Message.cpp
    Message.h
    MessageQueue.cpp
    MessageQueue.h
)

target_include_directories(android_os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_os PUBLIC object_model platform_windows)