cmake_minimum_required (VERSION 3.11)

find_package(GTest REQUIRED)
find_package(jungle_logstore REQUIRED)

add_executable (am_messaging_test)
target_sources(am_messaging_test PRIVATE
            MessagingTest.cpp
            test_state_manager.cpp
          )
target_link_libraries (am_messaging_test
      sds_messaging
      nuraft_grpc::nuraft_grpc
      jungle_logstore::jungle_logstore
      GTest::gmock
    )

add_test(NAME MessagingTest COMMAND am_messaging_test -csv 1)
set_property(TEST MessagingTest PROPERTY RUN_SERIAL 1)
