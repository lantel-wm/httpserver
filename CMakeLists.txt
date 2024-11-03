cmake_minimum_required(VERSION 3.12)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)

project(co_http LANGUAGES CXX)

add_executable(server src/server.cpp)

find_package(fmt)
target_link_libraries(server fmt::fmt)
