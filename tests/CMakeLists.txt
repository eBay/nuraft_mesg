set(HOMESTORE_CXX_FLAGS "-Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-empty-body -Wno-cast-function-type -Wno-deprecated-copy -Wno-attributes")
set_source_files_properties(MessagingTest.cpp PROPERTIES COMPILE_FLAGS ${HOMESTORE_CXX_FLAGS})

add_executable (am_messaging_test
                    MessagingTest.cpp
                    test_state_manager.cpp)
target_link_libraries (am_messaging_test
    sds_messaging
    ${CONAN_LIBS}
    )

add_test(NAME MessagingTest COMMAND am_messaging_test -csv 1)
set_property(TEST MessagingTest PROPERTY RUN_SERIAL 1)
