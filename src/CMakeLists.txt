cmake_minimum_required(VERSION 3.11)

find_package(grpc_helper REQUIRED)
find_package(nuraft REQUIRED)

add_subdirectory (proto)

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
    lib/grpc_client.cpp
    lib/grpc_factory.cpp
    lib/grpc_server.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}-proto>
  )
target_link_libraries(${PROJECT_NAME}
    grpc_helper::grpc_helper
    nuraft::nuraft
  )
target_include_directories(${PROJECT_NAME} PRIVATE "include")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
