add_library(cache_manager STATIC
    CacheManager.cpp
    CacheManager.h
)

target_include_directories(cache_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})