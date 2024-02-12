cmake_minimum_required (VERSION 3.11)

find_package(lz4)

file (GLOB LIBRARY_SOURCES *.cc)

add_flags("-w")

if (APPLE)
    set(OPEN_MEMSTREAM open_memstream.c)
    message(STATUS "APPLE: Use custom ${OPEN_MEMSTREAM}")
endif ()

add_library(jungle_logstore OBJECT ${LIBRARY_SOURCES} ${OPEN_MEMSTREAM})
target_link_libraries(jungle_logstore jungle::jungle nuraft::nuraft lz4::lz4)
