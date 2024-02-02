cmake_minimum_required(VERSION 3.11)

find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

include(${sisl_INCLUDE_DIRS}/../cmake/settings_gen.cmake)
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_flags("-Wall -Wextra -Werror -Wpedantic")

add_subdirectory (proto)
add_subdirectory (flatb)

add_subdirectory (lib)
if(DEFINED ENABLE_TESTING)
  if(${ENABLE_TESTING})
    add_subdirectory(tests)
  endif()
endif()
