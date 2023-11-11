cmake_minimum_required (VERSION 3.11)

find_package(GTest REQUIRED)
find_package(jungle QUIET)

if (jungle_FOUND)
    add_subdirectory(jungle_logstore)

    add_library(messaging_test OBJECT)
    target_sources(messaging_test PRIVATE
       MessagingTest.cpp
       test_state_manager.cpp
    )
    target_link_libraries(messaging_test
       ${PROJECT_NAME}
       jungle::jungle
       GTest::gmock
    )

    add_executable(messaging_test_proto)
    target_sources(messaging_test_proto PRIVATE
       $<TARGET_OBJECTS:messaging_test>
       $<TARGET_OBJECTS:jungle_logstore>
    )
    target_link_libraries (messaging_test_proto
       ${PROJECT_NAME}
       ${PROJECT_NAME}_proto
       jungle::jungle
       GTest::gmock
    )
    add_test(NAME MessagingTestProto COMMAND messaging_test_proto -cv 0)
    set_property(TEST MessagingTestProto PROPERTY RUN_SERIAL 1)
endif ()
