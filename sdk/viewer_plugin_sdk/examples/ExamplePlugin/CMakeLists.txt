cmake_minimum_required(VERSION 3.16)
project(ViewerSdkExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
add_subdirectory(../.. ViewerPluginSDK-build)

add_library(ViewerSdkExample MODULE ExamplePlugin.cpp)
target_link_libraries(ViewerSdkExample PRIVATE ViewerPluginSDK)
set_target_properties(ViewerSdkExample PROPERTIES PREFIX "")
add_custom_command(TARGET ViewerSdkExample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json
        $<TARGET_FILE_DIR:ViewerSdkExample>/plugin.json
)
