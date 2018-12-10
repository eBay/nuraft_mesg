set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CPP_WARNINGS} ${CMAKE_CXX_FLAGS}")

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            proto/raft_types.proto
                            proto/raft_service.proto
                        )

PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
                            proto/raft_service.proto
                        )

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_PATH ${FIRST_PROTO} DIRECTORY)
include_directories(BEFORE include ${PROTO_PATH})

set(LIBRARY_SOURCES
            lib/grpc_client.cpp
            lib/grpc_factory.cpp
            lib/grpc_server.cpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
            ${GRPC_SRCS}
            ${GRPC_HDRS}
        )
add_library(cornerstone_grpc ${LIBRARY_SOURCES})
conan_target_link_libraries(cornerstone_grpc)
