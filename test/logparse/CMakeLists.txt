add_executable(logparse_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/binary_log_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/binary_reader.cpp
    test_binary_log_parser.cpp
)

target_include_directories(logparse_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(logparse_test PRIVATE test_base)

target_compile_definitions(logparse_test PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

if(MSVC)
    target_compile_options(logparse_test PRIVATE /bigobj)
endif()
