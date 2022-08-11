cmake_minimum_required(VERSION 3.11)

add_library(${PROJECT_NAME}-proto OBJECT
    raft_types.proto
    raft_service.proto
  )
target_link_libraries(${PROJECT_NAME}-proto
      protobuf::libprotobuf
      gRPC::grpc++
  )

protobuf_generate(LANGUAGE cpp TARGET ${PROJECT_NAME}-proto PROTOS raft_types.proto)
protobuf_generate(LANGUAGE cpp TARGET ${PROJECT_NAME}-proto PROTOS raft_service.proto)

protobuf_generate(
    TARGET ${PROJECT_NAME}-proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
)
