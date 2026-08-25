# datamgr unit tests executable
add_executable(datamgr_test)

target_sources(datamgr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_viewer/datamgr/data_manager.cpp
    test_enum.cpp
    test_data_chunk.cpp
    test_classify_cell.cpp
    test_column_construct.cpp
    test_column_push.cpp
    test_column_lifecycle.cpp
    test_column_iterator.cpp
    test_column_convert.cpp
    test_column_misc.cpp
    test_data_manager_expr.cpp
)

target_include_directories(datamgr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
)

target_compile_definitions(datamgr_test PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

target_compile_options(datamgr_test PRIVATE /bigobj)

target_link_libraries(datamgr_test PRIVATE
    test_base
)

add_executable(plugin_data_extension_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_viewer/datamgr/data_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_viewer/datamgr/expression_engine.cpp
    test_plugin_derived_columns.cpp
    test_expression_engine.cpp
)

target_include_directories(plugin_data_extension_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
)

target_compile_definitions(plugin_data_extension_test PRIVATE VIEWER_LIB)
target_compile_options(plugin_data_extension_test PRIVATE /bigobj /utf-8)
target_link_libraries(plugin_data_extension_test PRIVATE test_base)
