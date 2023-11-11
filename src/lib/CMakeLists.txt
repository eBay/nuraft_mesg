cmake_minimum_required(VERSION 3.11)

include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}")

add_library(${PROJECT_NAME}-config OBJECT)
settings_gen_cpp($<TARGET_FILE:flatbuffers::flatc> ${CMAKE_CURRENT_BINARY_DIR}/generated/ ${PROJECT_NAME}-config nuraft_mesg_config.fbs)
target_link_libraries(${PROJECT_NAME}-config
   sisl::sisl
)

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
   manager_impl.cpp
   factory.cpp
   repl_service_ctx.cpp
   service.cpp
   grpc_server.cpp
   data_service_grpc.cpp
   $<TARGET_OBJECTS:${PROJECT_NAME}-config>
)
target_link_libraries(${PROJECT_NAME}
   sisl::sisl
   nuraft::nuraft
)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}-config)

add_subdirectory (proto)
add_subdirectory (flatb)
