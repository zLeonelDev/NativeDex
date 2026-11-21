cmake_minimum_required(VERSION 3.20)
project(bytecode_interpreter LANGUAGES CXX)

add_library(bytecode_interpreter STATIC
    BytecodeInterpreter.cpp
    BytecodeInterpreter.h
    Interpreter.cpp
    Vm.cpp
    Vm.h
    NativeRegistry.cpp
    NativeRegistry.h
    NativeHelpers.h
    CoreNatives.cpp
    CollectionNatives.cpp
    FrameworkNatives.cpp
    ReflectNatives.cpp
    Frame.cpp
    Frame.h
)

target_include_directories(bytecode_interpreter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bytecode_interpreter PUBLIC dex_loader class_loader object_model heap)
