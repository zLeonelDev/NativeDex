cmake_minimum_required(VERSION 3.20)
project(android_widget LANGUAGES CXX)

# No implementation files yet - placeholder
add_library(android_widget INTERFACE)

target_include_directories(android_widget INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(android_widget INTERFACE android_view object_model)