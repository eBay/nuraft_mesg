cmake_minimum_required(VERSION 3.11)
project(test_package)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(nuraft_mesg QUIET REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CPP_WARNINGS "-Wall -Wextra -Werror")

add_executable(example_server example_server.cpp example_state_manager.cpp in_memory_log_store.cpp)
target_link_libraries(example_server nuraft_mesg::nuraft_mesg)

add_executable(example_client example_client.cpp)
target_link_libraries(example_client nuraft_mesg::nuraft_mesg)
