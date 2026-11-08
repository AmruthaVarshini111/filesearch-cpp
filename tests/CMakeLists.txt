find_package(GTest QUIET)
if(NOT GTest_FOUND)
  message(STATUS "GoogleTest not found locally - fetching v1.14.0 (needs internet at configure time)")
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

add_executable(filesearch_tests
  test_string_utils.cpp
  test_inverted_index.cpp
  test_ranker.cpp
  test_index_store.cpp
  test_indexer.cpp
  test_search.cpp
  test_cli.cpp
)
target_link_libraries(filesearch_tests PRIVATE filesearch_core GTest::gtest GTest::gmock GTest::gtest_main)
target_compile_definitions(filesearch_tests PRIVATE
  FILESEARCH_SAMPLE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/sample_data")
gtest_discover_tests(filesearch_tests DISCOVERY_TIMEOUT 60)
