set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CPP_WARNINGS} ${CMAKE_CXX_FLAGS}")

add_subdirectory(proto)

add_library(sds_messaging
              $<TARGET_OBJECTS:mesg_pb>
              $<TARGET_OBJECTS:mesg_rpc>)
