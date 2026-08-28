add_executable(logparse_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/binary_log_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/binary_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/binary_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/rbt_log_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/ziplog/zip_archive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_logparse/ziplog/zip_entry_binary_input.cpp
    test_binary_log_parser.cpp
    test_rbt_log_parser.cpp
)

target_include_directories(logparse_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/libzip/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/zlib/include
)

target_link_libraries(logparse_test PRIVATE
    test_base
    ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/libzip/lib/zip.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/zlib/lib/z.lib
)

target_compile_definitions(logparse_test PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

if(MSVC)
    target_compile_options(logparse_test PRIVATE /bigobj /wd4819)
endif()

add_custom_command(TARGET logparse_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/libzip/bin/zip.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extra/zlib/bin/z.dll
        $<TARGET_FILE_DIR:logparse_test>
)
