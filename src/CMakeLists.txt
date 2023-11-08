cmake_minimum_required(VERSION 3.11)

find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

include(${sisl_INCLUDE_DIRS}/../cmake/settings_gen.cmake)
include_directories(BEFORE "include" ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

add_flags("-Wall -Wextra -Werror -Wpedantic")

add_subdirectory (lib)
add_subdirectory(tests)
