cmake_minimum_required (VERSION 3.11)

include_directories("${CONAN_INCLUDE_DIRS_NURAFT_GRPC}/nuraft_grpc")

find_package(nuraft_grpc REQUIRED)

# add version information
set(PACKAGE_REVISION "0.0.0+unknown")
if (DEFINED CONAN_PACKAGE_NAME)
  set(PACKAGE_REVISION "${CONAN_PACKAGE_VERSION}")
endif ()
add_flags("-DPACKAGE_NAME=\\\"${PROJECT_NAME}\\\"")
add_flags("-DPACKAGE_VERSION=\\\"${PACKAGE_REVISION}\\\"")

add_subdirectory(proto)

include_directories(BEFORE include ${CMAKE_CURRENT_BINARY_DIR}/proto "${CONAN_INCLUDE_DIRS_SDS_PROTO}/sds_proto")

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
              lib/factory.cpp
              lib/service.cpp
              lib/messaging.cpp
              $<TARGET_OBJECTS:mesg_rpc>)
target_link_libraries(${PROJECT_NAME} nuraft_grpc::nuraft_grpc)

add_subdirectory(tests)
