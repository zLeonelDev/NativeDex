add_library(method_resolver STATIC
    MethodResolver.cpp
    MethodResolver.h
)

target_include_directories(method_resolver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})