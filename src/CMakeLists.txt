PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            proto/raft_types.proto
                        )

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_PATH ${FIRST_PROTO} DIRECTORY)
include_directories(BEFORE include ${PROTO_PATH})

set(LIBRARY_SOURCES
            include/raft_core_grpc.hpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
        )
add_library(cornerstone_grpc ${LIBRARY_SOURCES})
conan_target_link_libraries(cornerstone_grpc)
