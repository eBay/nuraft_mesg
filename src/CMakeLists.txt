cmake_minimum_required(VERSION 3.11)

find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

list(GET sisl_INCLUDE_DIRS 0 SISL_TOP_INCLUDE)
include ("${SISL_TOP_INCLUDE}/../cmake/settings_gen.cmake")
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_flags("-Wall -Wextra -Werror -Wpedantic")

add_subdirectory (proto)
add_subdirectory (flatb)

add_subdirectory (lib)
add_subdirectory(tests)
