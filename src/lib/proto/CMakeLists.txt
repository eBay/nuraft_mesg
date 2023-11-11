cmake_minimum_required(VERSION 3.11)

add_library(${PROJECT_NAME}-proto-specs OBJECT
    raft_types.proto
    messaging_service.proto
  )
target_link_libraries(${PROJECT_NAME}-proto-specs
      protobuf::libprotobuf
      gRPC::grpc++
  )

protobuf_generate(LANGUAGE cpp TARGET ${PROJECT_NAME}-proto-specs PROTOS raft_types.proto)
protobuf_generate(LANGUAGE cpp TARGET ${PROJECT_NAME}-proto-specs PROTOS messaging_service.proto)

protobuf_generate(
   TARGET ${PROJECT_NAME}-proto-specs
   LANGUAGE grpc
   GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
   PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
)
target_link_libraries(${PROJECT_NAME}-proto-specs
   protobuf::libprotobuf
   gRPC::grpc++
)

add_library(${PROJECT_NAME}_proto)
target_sources(${PROJECT_NAME}_proto PRIVATE
   proto_client.cpp
   proto_mesg_factory.cpp
   proto_service.cpp
   $<TARGET_OBJECTS:${PROJECT_NAME}-proto-specs>
)
target_link_libraries(${PROJECT_NAME}_proto
   sisl::sisl
   nuraft::nuraft
   protobuf::libprotobuf
   gRPC::grpc++
)
target_include_directories(${PROJECT_NAME}_proto PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
