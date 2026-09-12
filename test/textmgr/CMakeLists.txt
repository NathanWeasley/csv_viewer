set(CMAKE_AUTOMOC ON)

add_executable(textmgr_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_viewer/textmgr/rbt_text_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../code_viewer/textmgr/rbt_text_manager.h
    test_rbt_text_manager.cpp
)

target_include_directories(textmgr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base
)

target_compile_definitions(textmgr_test PRIVATE VIEWER_LIB)
target_compile_options(textmgr_test PRIVATE /utf-8)

target_link_libraries(textmgr_test PRIVATE
    test_base
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)
