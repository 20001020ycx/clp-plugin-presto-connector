cmake_minimum_required(VERSION 3.22)

# A standalone project, deliberately not part of the plugin build: it must configure and run
# without Velox, Presto, or the `task`-installed dependencies. Anything reachable from these tests
# stays clear of Velox, so a header that reaches for it fails to compile here.
project(clp-plugin-velox-connector-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG "v1.17.0"
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_executable(clp-plugin-velox-connector-unit-tests
    ClpS3UrlTest.cpp
    ClpTimestampPrecisionTest.cpp
)

target_include_directories(clp-plugin-velox-connector-unit-tests
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(clp-plugin-velox-connector-unit-tests PRIVATE GTest::gtest_main)

gtest_discover_tests(clp-plugin-velox-connector-unit-tests)
