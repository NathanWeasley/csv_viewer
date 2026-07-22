cmake_minimum_required(VERSION 3.16)

add_library(ViewerPluginSDK INTERFACE)
target_compile_features(ViewerPluginSDK INTERFACE cxx_std_17)
target_include_directories(ViewerPluginSDK INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ViewerPluginSDK INTERFACE Qt6::Core Qt6::Widgets)

