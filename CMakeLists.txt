cmake_minimum_required(VERSION 3.10)
project(gateway C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) #所有可执行文件统一输出到 build/bin/
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

include_directories(${PROJECT_SOURCE_DIR}/utils) #全局头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(third_party)
add_subdirectory(utils) #告诉 CMake "去 utils/ 目录里找 CMakeLists.txt 并执行它"，实现模块化构建
add_subdirectory(core)
add_subdirectory(plugins)


