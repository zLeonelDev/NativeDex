cmake_minimum_required(VERSION 3.20)
project(launcher LANGUAGES CXX)

add_library(launcher_core STATIC
    core/Bootstrap.cpp
    core/Bootstrap.h
    core/AndroidManifest.cpp
    core/AndroidManifest.h
    core/AsyncLoader.cpp
    core/AsyncLoader.h
)

target_link_libraries(launcher_core PUBLIC miniz)

add_library(launcher_ui STATIC
    ui/WindowManager.cpp
    ui/WindowManager.h
    ui/ActivityManager.cpp
    ui/ActivityManager.h
    ui/Activity.cpp
    ui/Activity.h
    ui/View.h
    ui/NativeViewRegistry.cpp
    ui/NativeViewRegistry.h
    ui/NativeBridge.cpp
    ui/NativeBridge.h
    ui/LayoutInflater.cpp
    ui/LayoutInflater.h
    BinaryAndroidXmlParser.cpp
    BinaryAndroidXmlParser.h
)

add_executable(launcher WIN32
    main.cpp
)

target_link_options(launcher PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/launcher.map")

target_link_libraries(launcher PUBLIC
    launcher_core
    launcher_ui
    dex_loader
    class_loader
    bytecode_interpreter
    object_model
    heap
    gc
    api_registry
    thread_manager
    module_manager
    cache_manager
    logger
    android_app
    android_content
    android_view
    android_widget
    android_graphics
    android_os
    platform_windows
    miniz
)

target_include_directories(launcher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# Avoid Windows headers defining min/max macros that break std::min/std::max
if(MSVC)
    target_compile_definitions(launcher_core PUBLIC NOMINMAX=1)
    target_compile_definitions(launcher_ui PUBLIC NOMINMAX=1)
endif()