# test_base static library — the test framework
add_library(test_base STATIC
    test_runner.cpp
)

target_include_directories(test_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/.
)