cmake_minimum_required(VERSION 3.20)
project(android_app LANGUAGES CXX)

add_library(android_app STATIC
    Activity.cpp
    Activity.h
)

target_include_directories(android_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_app PUBLIC android_os android_content android_view android_widget android_graphics object_model heap)