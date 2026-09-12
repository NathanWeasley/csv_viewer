cmake_minimum_required(VERSION 3.16)
project(csv_viewer_test VERSION 1.0.0 LANGUAGES CXX)

# C++17 required (same as main project)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include paths — point to project root so #include "code_viewer/..." works
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Subdirectories
add_subdirectory(base)
add_subdirectory(datamgr)
add_subdirectory(logparse)

# RBT 文本管理层依赖 Qt；未配置 Qt 开发环境时不影响其余纯 C++ 测试。
find_package(Qt6 QUIET COMPONENTS Core Gui Concurrent)
if(Qt6_FOUND)
    add_subdirectory(textmgr)
else()
    message(STATUS "Qt6 not found: skipping textmgr_test")
endif()
