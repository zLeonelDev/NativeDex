cmake_minimum_required(VERSION 3.20)
project(nativedex_tests LANGUAGES CXX)

add_executable(test_gc
    test_gc.cpp
)

target_link_libraries(test_gc PRIVATE heap object_model dex_loader)

# Add test runner
add_custom_target(run_tests
    COMMAND test_gc
    DEPENDS test_gc
)