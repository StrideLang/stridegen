cmake_minimum_required(VERSION 3.8)
project(StrideGen_tests)

option(STRIDEPARSER_BUILD_TESTS "Build and run unit test for the Stride parser" OFF)

# Get and build googletest
if (NOT gtest)
    message("Building googletest with StrideGen build")
    include(FetchContent)
    FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.12.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()


set(TEST_SOURCES
  main.cpp
  resolver.cpp
  compiler_properties.cpp
  codeanalysis_tests.cpp
  codevalidator_tests.cpp
  stridesystem_tests.cpp
)


add_executable(stridegen_tests ${TEST_SOURCES})

set_target_properties(stridegen_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_test(NAME stridegen_tests COMMAND stridegen_tests)

target_link_libraries(stridegen_tests PUBLIC StrideParser StrideUtils StrideGen gtest)
target_compile_definitions(stridegen_tests PUBLIC
  "-DTESTS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/\"")

if (STRIDEGEN_CODE_COVERAGE)
  set_target_properties(stridegen_tests PROPERTIES COMPILE_FLAGS "-fPIE -g -O0 -Wall -fprofile-arcs -ftest-coverage")
  target_link_libraries(stridegen_tests PUBLIC gcov)
endif()
