cmake_minimum_required(VERSION 3.20)
project(class_loader LANGUAGES CXX)

add_library(class_loader STATIC
    ClassLoader.cpp
    ClassLoader.h
)

target_include_directories(class_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(class_loader PUBLIC dex_loader object_model)