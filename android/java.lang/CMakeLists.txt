cmake_minimum_required(VERSION 3.20)
project(android_java_lang LANGUAGES CXX)

add_library(android_java_lang STATIC
    JavaString.cpp
    JavaString.h
    System.cpp
    System.h
)

target_include_directories(android_java_lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_java_lang PUBLIC object_model platform_windows)