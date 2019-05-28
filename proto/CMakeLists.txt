
list(APPEND PROTOBUF_IMPORT_DIRS
          "${CONAN_NUPILLAR_GRPC_ROOT}/proto"
        )
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            messaging_service.proto)
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
                            messaging_service.proto)


list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_PATH ${FIRST_PROTO} DIRECTORY)
set(GRPC_INC ${PROTO_PATH} PARENT_SCOPE)

add_library(mesg_pb OBJECT ${PROTO_SRCS})
add_library(mesg_rpc OBJECT ${GRPC_SRCS})
