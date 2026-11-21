cmake_minimum_required(VERSION 3.20)
project(platform_windows LANGUAGES CXX)

add_library(platform_windows STATIC
    window/Win32Window.cpp
    window/Win32Window.h
    graphics/Direct2DGraphics.cpp
    graphics/Direct2DGraphics.h
    input/Win32Input.cpp
    input/Win32Input.h
    threading/Win32Thread.cpp
    threading/Win32Thread.h
    filesystem/Win32FileSystem.cpp
    filesystem/Win32FileSystem.h
    time/Win32Time.cpp
    time/Win32Time.h
)

target_include_directories(platform_windows PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/window
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/input
    ${CMAKE_CURRENT_SOURCE_DIR}/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem
    ${CMAKE_CURRENT_SOURCE_DIR}/time
)

target_link_libraries(platform_windows PUBLIC 
    d2d1 
    dwrite 
    windowscodecs 
    shlwapi
)

target_compile_definitions(platform_windows PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)