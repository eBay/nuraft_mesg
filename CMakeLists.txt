set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CPP_WARNINGS} ${CMAKE_CXX_FLAGS}")

include_directories("${CONAN_INCLUDE_DIRS_RAFT_CORE_GRPC}/raft_core_grpc")

add_subdirectory(proto)

include_directories(BEFORE include ${GRPC_INC} "${CONAN_INCLUDE_DIRS_SDS_PROTO}/sds_proto")

add_library(sds_messaging
              lib/factory.cpp
              lib/service.cpp
              $<TARGET_OBJECTS:mesg_pb>
              $<TARGET_OBJECTS:mesg_rpc>)
