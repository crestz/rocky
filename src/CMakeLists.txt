cmake_minimum_required(VERSION 4.0.0)

file(GLOB_RECURSE LIB_SOURCES "*.cxx")

add_library("librocky"
  ${LIB_SOURCES})

target_compile_features("librocky" PUBLIC cxx_std_20)

message(NOTICE "-- Build Tests: ${ROCKY_BUILD_TESTS}")

if(ROCKY_BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)

  include(GoogleTest)

  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPS "*_test.cc")

  foreach(test_path ${TEST_SOURCES})
    # Get relative path to create a unique name, e.g. "tests_math_adder_test"
    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}" "${test_path}")

    message(STATUS "Found Test File: ${test_path}")

    # Replace slashes and dots with underscores to make a valid CMake target name.
    string(REPLACE "/" "_" target_name "${rel_path}")
    string(REPLACE "." "_" target_name "${target_name}")

    add_executable(${target_name} ${test_path})

    target_link_libraries(${target_name}
      PRIVATE
      librocky
      GTest::gtest_main)

    target_compile_options(${target_name}
      PRIVATE
      -fsanitize=thread
      -fno-omit-frame-pointer)

    target_link_options(${target_name}
      PRIVATE
      -fsanitize=thread)

    gtest_discover_tests(${target_name})
  endforeach()
endif()
