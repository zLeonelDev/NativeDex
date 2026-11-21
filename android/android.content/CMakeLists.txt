cmake_minimum_required(VERSION 3.20)
project(android_content LANGUAGES CXX)

add_library(android_content STATIC
    Intent.cpp
    Intent.h
)

target_include_directories(android_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_content PUBLIC android_os object_model platform_windows)