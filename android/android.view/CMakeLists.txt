cmake_minimum_required(VERSION 3.20)
project(android_view LANGUAGES CXX)

add_library(android_view STATIC
    View.cpp
    View.h
)

target_include_directories(android_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_view PUBLIC android_graphics object_model platform_windows)