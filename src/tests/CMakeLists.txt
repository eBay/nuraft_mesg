cmake_minimum_required (VERSION 3.11)

find_package(jungle QUIET REQUIRED)
find_package(lz4 QUIET REQUIRED)

add_subdirectory(jungle_logstore)

add_library(test_fixture OBJECT)
target_sources(test_fixture PRIVATE
  test_state_manager.cpp
)
target_link_libraries(test_fixture
  sisl::sisl
  nuraft::nuraft
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
  ${PROJECT_NAME}_proto
  jungle::jungle
  LZ4::lz4_static
  GTest::gmock
)
add_test(NAME RaftServiceTest COMMAND raft_service_test -cv 2)
set_property(TEST RaftServiceTest PROPERTY RUN_SERIAL 1)
# allow_user_poisoning=0: gRPC/absl's InlinedVector calls ASAN_POISON_MEMORY_REGION
# (f7 bytes) on stack slots for container overflow tracking. Those f7 bytes persist
# across stack frame reuse; grpc_core::StatusCreate/SetPayload/basic_string lands on
# them and triggers use-after-poison. allow_user_poisoning=0 makes the explicit
# __asan_poison_memory_region calls no-ops, eliminating the false positive.
# detect_container_overflow=0: suppresses ASan's own fc-byte container tracking for
# gRPC's std containers.
set_tests_properties(RaftServiceTest PROPERTIES
  ENVIRONMENT
    "ASAN_OPTIONS=allow_user_poisoning=0:detect_container_overflow=0;TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp")

add_executable(data_service_test)
target_sources(data_service_test PRIVATE
  data_service_tests.cpp
  $<TARGET_OBJECTS:test_fixture>
  $<TARGET_OBJECTS:jungle_logstore>
)
target_link_libraries (data_service_test
  ${PROJECT_NAME}_proto
  jungle::jungle
  LZ4::lz4_static
  GTest::gmock
)
add_test(NAME DataServiceTest COMMAND data_service_test -cv 1)
set_property(TEST DataServiceTest PROPERTY RUN_SERIAL 1)
set_tests_properties(DataServiceTest PROPERTIES
  ENVIRONMENT
    "ASAN_OPTIONS=allow_user_poisoning=0:detect_container_overflow=0;TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp")

add_executable(thread_pool_test)
target_sources(thread_pool_test PRIVATE
  thread_pool_tests.cpp
  $<TARGET_OBJECTS:test_fixture>
  $<TARGET_OBJECTS:jungle_logstore>
)
target_link_libraries (thread_pool_test
  ${PROJECT_NAME}_proto
  jungle::jungle
  LZ4::lz4_static
  GTest::gmock
)
add_test(NAME ThreadPoolTest COMMAND thread_pool_test -cv 2)
set_property(TEST ThreadPoolTest PROPERTY RUN_SERIAL 1)
set_tests_properties(ThreadPoolTest PROPERTIES
  ENVIRONMENT
    "ASAN_OPTIONS=allow_user_poisoning=0:detect_container_overflow=0;TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp")
