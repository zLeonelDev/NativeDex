add_library(module_manager STATIC
    ModuleManager.cpp
    ModuleManager.h
)

target_include_directories(module_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})