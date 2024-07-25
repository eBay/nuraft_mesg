cmake_minimum_required(VERSION 3.11)

add_library(${PROJECT_NAME}-core OBJECT)
target_sources(${PROJECT_NAME}-core PRIVATE
   manager_impl.cpp
   factory.cpp
   repl_service_ctx.cpp
   service.cpp
   grpc_server.cpp
)
target_link_libraries(${PROJECT_NAME}-core
   sisl::sisl
   nuraft::nuraft
)

add_library(${PROJECT_NAME}-data-svc OBJECT)
target_sources(${PROJECT_NAME}-data-svc PRIVATE
   data_service_grpc.cpp
)
settings_gen_cpp($<TARGET_FILE:flatbuffers::flatc> ${CMAKE_CURRENT_BINARY_DIR}/generated/ ${PROJECT_NAME}-data-svc nuraft_mesg_config.fbs)
target_link_libraries(${PROJECT_NAME}-data-svc
   sisl::sisl
   nuraft::nuraft
)
