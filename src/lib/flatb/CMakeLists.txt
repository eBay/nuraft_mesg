cmake_minimum_required(VERSION 3.11)

list(APPEND SCHEMA_FLAGS "--scoped-enums" "--gen-name-strings" "--cpp-std=c++17" "--cpp-static-reflection")

flatbuffers_generate_headers(
    TARGET fbschemas
    SCHEMAS "raft_types.fbs"
    FLAGS ${SCHEMA_FLAGS}
)
flatbuffers_generate_headers(
    TARGET fbservice
    SCHEMAS "messaging_service.fbs"
    FLAGS "--grpc" ${SCHEMA_FLAGS}
)

add_library(${PROJECT_NAME}_flatb OBJECT)
target_sources(${PROJECT_NAME}_flatb PRIVATE
    flatb_client.cpp
    $<TARGET_PROPERTY:fbservice,INTERFACE_SOURCES>
)
target_include_directories(${PROJECT_NAME}_flatb PRIVATE
    $<TARGET_PROPERTY:fbschemas,INTERFACE_INCLUDE_DIRECTORIES>
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(${PROJECT_NAME}_flatb
    sisl::sisl
    nuraft::nuraft
    flatbuffers::flatbuffers
    gRPC::grpc++
)
add_dependencies(${PROJECT_NAME}_flatb GENERATE_fbservice)
add_dependencies(${PROJECT_NAME}_flatb GENERATE_fbschemas)
