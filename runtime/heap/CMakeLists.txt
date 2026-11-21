add_library(heap STATIC
    HeapManager.cpp
    HeapManager.h
)

target_include_directories(heap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/runtime/object_model
)

target_link_libraries(heap PRIVATE object_model)