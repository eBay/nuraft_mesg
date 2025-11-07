cmake_minimum_required(VERSION 3.11)

set(PACKAGE_REVISION "0.0.0+unknown")
if (DEFINED CONAN_PACKAGE_NAME)
    set(PACKAGE_REVISION "${CONAN_PACKAGE_VERSION}")
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPACKAGE_VERSION=\\\"${PACKAGE_REVISION}\\\"")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPACKAGE_NAME=\\\"${PROJECT_NAME}\\\"")


find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

list(GET sisl_INCLUDE_DIRS 0 SISL_TOP_INCLUDE)
include("${SISL_TOP_INCLUDE}/../cmake/settings_gen.cmake")
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_flags("-Wall -Wextra -Werror -Wpedantic")

add_subdirectory(proto)
add_subdirectory(flatb)

add_subdirectory(lib)
add_subdirectory(tests)
