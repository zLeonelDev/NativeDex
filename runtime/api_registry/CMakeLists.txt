add_library(api_registry STATIC
    APIRegistry.cpp
    APIRegistry.h
)

target_include_directories(api_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})