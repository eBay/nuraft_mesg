cmake_minimum_required(VERSION 3.11)

find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

include(${sisl_INCLUDE_DIRS}/../cmake/settings_gen.cmake)

add_subdirectory (proto)

add_flags("-Wall -Wextra -Werror -Wpedantic")

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
    lib/grpc_client.cpp
    lib/grpc_factory.cpp
    lib/grpc_server.cpp
    lib/mesg_client.cpp
    lib/data_service_grpc.cpp
    lib/manager_impl.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}-proto>
  )
settings_gen_cpp($<TARGET_FILE:flatbuffers::flatc> ${CMAKE_CURRENT_BINARY_DIR}/generated/ ${PROJECT_NAME} lib/nuraft_mesg_config.fbs)
target_link_libraries(${PROJECT_NAME}
    sisl::sisl
    nuraft::nuraft
  )
target_include_directories(${PROJECT_NAME} PUBLIC "include")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_subdirectory(tests)
