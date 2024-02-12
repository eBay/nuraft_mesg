cmake_minimum_required(VERSION 3.11)
project(test_package)

find_package(NuraftMesg QUIET REQUIRED)

add_executable(example_server example_server.cpp example_state_manager.cpp in_memory_log_store.cpp)
target_compile_features(example_server PUBLIC cxx_std_20)
target_link_libraries(example_server nuraft_mesg::proto)

add_executable(example_client example_client.cpp)
target_compile_features(example_client PUBLIC cxx_std_20)
target_link_libraries(example_client nuraft_mesg::proto)
