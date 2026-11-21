cmake_minimum_required(VERSION 3.20)
project(android_graphics LANGUAGES CXX)

# No implementation files yet - placeholder
add_library(android_graphics INTERFACE)

target_include_directories(android_graphics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_graphics INTERFACE platform_windows object_model)