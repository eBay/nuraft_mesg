cmake_minimum_required (VERSION 3.11)

find_package(jungle QUIET)

if (jungle_FOUND)
   find_package(GTest REQUIRED)
   add_subdirectory(jungle_logstore)

   add_library(test_fixture OBJECT)
   target_sources(test_fixture PRIVATE
      test_state_manager.cpp
   )
   target_link_libraries(test_fixture
      ${PROJECT_NAME}
      jungle::jungle
      GTest::gmock
   )

   add_executable(raft_service_test)
   target_sources(raft_service_test PRIVATE
       raft_service_tests.cpp
      $<TARGET_OBJECTS:test_fixture>
      $<TARGET_OBJECTS:jungle_logstore>
   )
   target_link_libraries (raft_service_test
      ${PROJECT_NAME}
      ${PROJECT_NAME}_proto
      jungle::jungle
      GTest::gmock
   )
   add_test(NAME RaftServiceTest COMMAND raft_service_test -cv 2)
   set_property(TEST RaftServiceTest PROPERTY RUN_SERIAL 1)

   add_executable(data_service_test)
   target_sources(data_service_test PRIVATE
      data_service_tests.cpp
      $<TARGET_OBJECTS:test_fixture>
      $<TARGET_OBJECTS:jungle_logstore>
   )
   target_link_libraries (data_service_test
      ${PROJECT_NAME}
      ${PROJECT_NAME}_proto
      jungle::jungle
      GTest::gmock
   )
   add_test(NAME DataServiceTest COMMAND data_service_test -cv 1)
   set_property(TEST DataServiceTest PROPERTY RUN_SERIAL 1)
endif ()
