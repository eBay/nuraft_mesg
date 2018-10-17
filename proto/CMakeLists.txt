
list(APPEND PROTOBUF_IMPORT_DIRS
          "${CONAN_RAFT_CORE_GRPC_ROOT}/proto"
          "${CONAN_OM-SVCLAYER_ROOT}/proto"
        )
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            messaging_service.proto)
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
                            messaging_service.proto)

add_library(mesg_pb OBJECT ${PROTO_SRCS})
add_library(mesg_rpc OBJECT ${GRPC_SRCS})
