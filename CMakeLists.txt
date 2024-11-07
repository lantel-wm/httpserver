cmake_minimum_required(VERSION 3.12)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    # set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(co_http LANGUAGES CXX)

# add src/*.cpp
file(GLOB_RECURSE server_src ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(server ${server_src})


target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/include)

find_package(fmt REQUIRED)
target_link_libraries(server fmt::fmt)
target_link_libraries(server pthread)
