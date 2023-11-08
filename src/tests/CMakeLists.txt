cmake_minimum_required (VERSION 3.11)

find_package(GTest REQUIRED)
find_package(jungle QUIET)

if (jungle_FOUND)
    add_subdirectory(jungle_logstore)

    add_executable (messaging_test)
    target_sources(messaging_test PRIVATE
                MessagingTest.cpp
                test_state_manager.cpp
              )
    target_link_libraries (messaging_test
          ${PROJECT_NAME}
          ${PROJECT_NAME}_proto
          $<TARGET_OBJECTS:jungle_logstore>
          jungle::jungle
          GTest::gmock
        )

    add_test(NAME MessagingTest COMMAND messaging_test -cv 0)
    set_property(TEST MessagingTest PROPERTY RUN_SERIAL 1)
endif ()
