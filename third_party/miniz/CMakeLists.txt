cmake_minimum_required(VERSION 3.20)
project(miniz LANGUAGES C)

add_library(miniz STATIC
    miniz.c
    miniz.h
)

target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})