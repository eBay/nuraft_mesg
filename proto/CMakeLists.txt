
list(APPEND PROTOBUF_IMPORT_DIRS
          "${CONAN_RAFT_CORE_GRPC_ROOT}/proto"
          "${CONAN_SDS_PROTO_ROOT}/proto"
        )
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            messaging_service.proto)
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
                            messaging_service.proto)

include_directories("${CONAN_INCLUDE_DIRS_SDS_PROTO}/sds_proto")

add_library(mesg_pb OBJECT ${PROTO_SRCS})
add_library(mesg_rpc OBJECT ${GRPC_SRCS})
