cmake_minimum_required (VERSION 3.11)

find_package(GTest REQUIRED)
find_package(jungle_logstore)

if (jungle_logstore_FOUND)
    add_executable (messaging_test)
    target_sources(messaging_test PRIVATE
                MessagingTest.cpp
                test_state_manager.cpp
              )
    target_link_libraries (messaging_test
          nuraft_mesg
          jungle_logstore::jungle_logstore
          GTest::gmock
        )

    add_test(NAME MessagingTest COMMAND messaging_test -csv 3)
    set_property(TEST MessagingTest PROPERTY RUN_SERIAL 1)
endif ()
