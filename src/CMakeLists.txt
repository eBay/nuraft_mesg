cmake_minimum_required(VERSION 3.11)

find_package(sisl REQUIRED)
find_package(nuraft REQUIRED)

add_subdirectory (proto)

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
    lib/grpc_client.cpp
    lib/grpc_factory.cpp
    lib/grpc_server.cpp
    lib/mesg_factory.cpp
    lib/service.cpp
    lib/data_service.cpp
    lib/messaging.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}-proto>
  )
target_link_libraries(${PROJECT_NAME}
    sisl::sisl
    nuraft::nuraft
  )
target_include_directories(${PROJECT_NAME} PUBLIC "include")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_subdirectory(tests)
