cmake_minimum_required(VERSION 3.15)

project(nuraft_mesg CXX)
enable_testing()

include(cmake/Flags.cmake)

set(CMAKE_CXX_STANDARD 23)

enable_testing()
find_package(GTest QUIET REQUIRED)

if (DEFINED BUILD_COVERAGE)
    if (${BUILD_COVERAGE})
        include(cmake/CodeCoverage.cmake)
        APPEND_COVERAGE_COMPILER_FLAGS()
        SETUP_TARGET_FOR_COVERAGE_GCOVR_XML(NAME coverage EXECUTABLE ctest DEPENDENCIES)
    endif ()
endif ()

if ((DEFINED THREAD_SANITIZER_ON) AND (${THREAD_SANITIZER_ON}))
    set(SANITIZER_TYPE "thread")
    include(cmake/sanitize.cmake)
elseif ((DEFINED ADDRESS_SANITIZER_ON) AND (${ADDRESS_SANITIZER_ON}))
    set(SANITIZER_TYPE "address")
    include(cmake/sanitize.cmake)
endif ()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

# --- stdexec (NVIDIA std::execution reference impl) ---
# Header-only; the coroutine data-service path (sisl::async::task = exec::task) uses it, exposed in
# public headers (common.hpp -> mesg_state_mgr.hpp/mesg_factory.hpp). Provided by FetchContent
# (mirrors iomgr; a Conan recipe may replace this later). stdexec's own top-level CMakeLists needs
# rapids-cmake, so we download the source tree only and expose the headers as an INTERFACE target.
# Pass -DSTDEXEC_SOURCE_DIR=<path> to use a pre-fetched tree and avoid network at configure time.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
if(NOT STDEXEC_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(
        stdexec
        GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
        GIT_TAG        ea0d8788da51a1671daa00b88bf238725f0844a8  # nvhpc-25.09
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(stdexec)
    if(NOT stdexec_POPULATED)
        FetchContent_Populate(stdexec)  # download only — do not run stdexec's CMakeLists.txt
    endif()
    set(STDEXEC_SOURCE_DIR ${stdexec_SOURCE_DIR})
endif()
add_library(stdexec_iface INTERFACE)
target_include_directories(stdexec_iface INTERFACE ${STDEXEC_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # -Wno-empty-body: stdexec internals. -Wno-subobject-linkage: stdexec coroutine frames legitimately
    # embed local lambda-closure types (no linkage); each TU's instantiation is unique so there is no
    # real ODR hazard, but GCC's heuristic flags it.
    target_compile_options(stdexec_iface INTERFACE -Wno-empty-body -Wno-subobject-linkage)
endif()
# stdexec's <execution> include drags in libstdc++'s PSTL, which defaults to the TBB backend and emits
# unresolved tbb::detail::r1::* symbols; force the serial backend.
target_compile_options(stdexec_iface INTERFACE -U_PSTL_PAR_BACKEND_TBB)
target_compile_definitions(stdexec_iface INTERFACE _PSTL_PAR_BACKEND_SERIAL)
add_library(STDEXEC::stdexec ALIAS stdexec_iface)
# Public headers now include stdexec; propagate it (include dirs + PSTL/warning flags) to every target.
link_libraries(stdexec_iface)

include_directories(BEFORE "include")
add_subdirectory(src)

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
