project(test_package)
cmake_minimum_required(VERSION 2.8.11)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

find_package(Protobuf REQUIRED)
include(cmake/grpc.cmake)

list(APPEND PROTOBUF_IMPORT_DIRS
            "${CONAN_RAFT_CORE_GRPC_ROOT}/proto"
        )
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
        proto/example_service.proto
        )

PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
        proto/example_service.proto
        )

set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror")

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_PATH ${FIRST_PROTO} DIRECTORY)
include_directories(BEFORE include ${PROTO_PATH})
add_library(example_lib OBJECT ${GRPC_SRCS} ${GRPC_HDRS} ${PROTO_SRCS} ${PROTO_HDRS})

add_executable(raft_server example_server.cpp state_manager.cpp $<TARGET_OBJECTS:example_lib>)
add_executable(raft_client example_client.cpp $<TARGET_OBJECTS:example_lib>)

conan_target_link_libraries(raft_server)
conan_target_link_libraries(raft_client)
