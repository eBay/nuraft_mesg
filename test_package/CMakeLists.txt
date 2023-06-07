project(test_package)
cmake_minimum_required(VERSION 2.8.11)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  if (NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "clang" AND NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "apple-clang")
    include (../cmake/debug_flags.cmake)
  endif()
endif()
if  (${MEMORY_SANITIZER_ON})
  include (../cmake/mem_sanitizer.cmake)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror")

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

add_executable(raft_server example_server.cpp example_state_manager.cpp in_memory_log_store.cpp)
add_executable(raft_client example_client.cpp)

conan_target_link_libraries(raft_server)
conan_target_link_libraries(raft_client)
