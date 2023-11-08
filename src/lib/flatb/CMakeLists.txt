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

add_library(${PROJECT_NAME}-flatb OBJECT)
target_sources(${PROJECT_NAME}-flatb PRIVATE
    $<TARGET_PROPERTY:fbservice,INTERFACE_SOURCES>
)
target_include_directories(${PROJECT_NAME}-flatb PUBLIC
    $<TARGET_PROPERTY:fbschemas,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(${PROJECT_NAME}-flatb
    flatbuffers::flatbuffers
    gRPC::grpc++
)
add_dependencies(${PROJECT_NAME}-flatb GENERATE_fbservice)
add_dependencies(${PROJECT_NAME}-flatb GENERATE_fbschemas)
