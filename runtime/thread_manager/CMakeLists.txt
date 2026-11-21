add_library(thread_manager STATIC
    ThreadManager.cpp
    ThreadManager.h
)

target_include_directories(thread_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})