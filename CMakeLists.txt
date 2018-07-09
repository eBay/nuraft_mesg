set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CPP_WARNINGS} ${CMAKE_CXX_FLAGS}")

list(APPEND PROTOBUF_IMPORT_DIRS
          "${CONAN_OM-SVCLAYER_ROOT}/idl"
          "${CONAN_RAFT_CORE_GRPC_ROOT}/proto"
        )
include_directories("${CONAN_INCLUDE_DIRS_OM-SVCLAYER}/om-svclayer")
include_directories("${CONAN_INCLUDE_DIRS_RAFT_CORE}/cornerstone")
include_directories("${CONAN_INCLUDE_DIRS_RAFT_CORE_GRPC}/cornerstone")
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
                            proto/raft_messages.proto
                            proto/sds_messages.proto
                            proto/messaging_service.proto)
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS
                            proto/messaging_service.proto)

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_PATH ${FIRST_PROTO} DIRECTORY)
set(LIBRARY_SOURCES
            ${GRPC_SRCS}
            ${GRPC_HDRS}
            ${PROTO_SRCS}
            ${PROTO_HDRS}
        )
add_library(sds_messaging ${LIBRARY_SOURCES})
target_sources(sds_messaging INTERFACE include/sds_messaging.h)
