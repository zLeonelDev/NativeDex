cmake_minimum_required(VERSION 3.20)
project(object_model LANGUAGES CXX)

add_library(object_model STATIC
    Object.cpp
    Object.h
    ClassObject.cpp
    ClassObject.h
    StringObject.cpp
    StringObject.h
    ArrayObject.cpp
    ArrayObject.h
    Reference.cpp
    Reference.h
)

target_link_libraries(object_model PUBLIC dex_loader)
target_include_directories(object_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})