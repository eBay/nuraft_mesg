PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            proto/raft_types.proto
                        )


set(LIBRARY_SOURCES
            include/raft_core_grpc.hpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
        )
add_library(cornerstone_grpc ${LIBRARY_SOURCES})
conan_target_link_libraries(cornerstone_grpc)
