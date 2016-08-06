cmake_minimum_required(VERSION 2.6)
project(Lockfree)
enable_testing()
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(.)

add_executable(runUnitTests test/basic.cpp test/index.cpp test/threads.cpp)
target_compile_features(runUnitTests PRIVATE cxx_range_for)
target_link_libraries(runUnitTests gtest gtest_main pthread)
add_test(NAME that-test-I-made COMMAND runUnitTests)

set(Lockfree_Version_Major 0)
set(Lockfree_Version_Minor 1)

configure_file (
  "${PROJECT_SOURCE_DIR}/version.h.in"
  "${PROJECT_BINARY_DIR}/version.h"
)
