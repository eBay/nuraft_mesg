cmake_minimum_required(VERSION 3.15)
project(nuraft_mesg CXX)
enable_testing()

include (cmake/Flags.cmake)

set(CMAKE_CXX_STANDARD 20)

enable_testing()
find_package(GTest QUIET REQUIRED)

if (DEFINED BUILD_COVERAGE)
  if (${BUILD_COVERAGE})
    include (cmake/CodeCoverage.cmake)
    APPEND_COVERAGE_COMPILER_FLAGS()
    SETUP_TARGET_FOR_COVERAGE_GCOVR_XML(NAME coverage EXECUTABLE ctest DEPENDENCIES )
  endif ()
endif ()

if (DEFINED MEMORY_SANITIZER_ON)
  if  (${MEMORY_SANITIZER_ON})
    include (cmake/mem_sanitizer.cmake)
  endif ()
endif()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

include_directories(BEFORE "include")
add_subdirectory (src)

# build info
string(TOUPPER "${CMAKE_BUILD_TYPE}" UC_CMAKE_BUILD_TYPE)
message(STATUS "Build configuration:       ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler info:           ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} ${CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN}")
message(STATUS "C++ compiler info:         ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN}")
message(STATUS "C flags:                   ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "C++ flags:                 ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (executable): ${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (shared):     ${CMAKE_SHARED_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (module):     ${CMAKE_MODULE_LINKER_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (static):     ${CMAKE_STATIC_LINKER_FLAGS} ${CMAKE_STATIC_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
