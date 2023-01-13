cmake_minimum_required (VERSION 3.11)

add_library(mesg_rpc OBJECT 
            messaging_service.proto
          )
target_link_libraries(mesg_rpc
      protobuf::libprotobuf
      gRPC::grpc++
  )

protobuf_generate(
            LANGUAGE cpp
            TARGET mesg_rpc
            IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" "${nuraft_grpc_LIB_DIRS}/../proto"
          )

protobuf_generate(
    TARGET mesg_rpc
    LANGUAGE grpc
    IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" "${nuraft_grpc_LIB_DIRS}/../proto"
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
)
