project(test_package)
cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_VERBOSE_MAKEFILE TRUE)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror")

file(GLOB SOURCE_FILES *.cpp)

add_executable(raft_server example_server.cpp)
add_executable(raft_client example_client.cpp)

target_link_libraries(raft_server ${CONAN_LIBS} ssl crypto z)
target_link_libraries(raft_client ${CONAN_LIBS} ssl crypto z)
