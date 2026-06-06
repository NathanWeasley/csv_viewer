# datamgr unit tests executable
add_executable(datamgr_test)

target_sources(datamgr_test PRIVATE
    test_enum.cpp
    test_data_chunk.cpp
    test_classify_cell.cpp
    test_column_construct.cpp
    test_column_push.cpp
    test_column_lifecycle.cpp
    test_column_iterator.cpp
    test_column_convert.cpp
    test_column_misc.cpp
)

target_include_directories(datamgr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
)

target_link_libraries(datamgr_test PRIVATE
    test_base
)