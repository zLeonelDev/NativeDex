cmake_minimum_required(VERSION 3.20)
project(dex_loader LANGUAGES CXX)

add_library(dex_loader STATIC
    DexLoader.cpp
    DexLoader.h
    BinaryReader.cpp
    BinaryReader.h
    DexFile.cpp
    DexFile.h
    DexHeader.cpp
    DexHeader.h
    StringTable.cpp
    StringTable.h
    TypeTable.cpp
    TypeTable.h
    ProtoTable.cpp
    ProtoTable.h
    FieldTable.cpp
    FieldTable.h
    MethodTable.cpp
    MethodTable.h
    ClassTable.cpp
    ClassTable.h
    Parser.cpp
    Parser.h
)

target_include_directories(dex_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})