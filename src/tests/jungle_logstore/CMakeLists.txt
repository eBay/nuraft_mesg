cmake_minimum_required (VERSION 3.11)

file (GLOB LIBRARY_SOURCES *.cc)

if (APPLE)
    set(OPEN_MEMSTREAM open_memstream.c)
    message(STATUS "APPLE: Use custom ${OPEN_MEMSTREAM}")
endif ()

add_library(jungle_logstore OBJECT ${LIBRARY_SOURCES} ${OPEN_MEMSTREAM})
target_link_libraries(jungle_logstore jungle::jungle nuraft::nuraft lz4::lz4)
