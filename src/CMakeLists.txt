cmake_minimum_required(VERSION 4.0.0)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "*.cxx")

add_library("librocky"
  ${LIB_SOURCES})

target_compile_features("librocky" PUBLIC cxx_std_20)

message(NOTICE "-- Build Tests: ${ROCKY_BUILD_TESTS}")

if(ROCKY_BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)

  include(GoogleTest)

  function(add_rocky_test test_path)
    set(options LINK_LIBROCKY)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}" "${test_path}")

    message(STATUS "Found Test File: ${test_path}")

    string(REPLACE "/" "_" target_name "${rel_path}")
    string(REPLACE "." "_" target_name "${target_name}")

    add_executable(${target_name} ${test_path})

    target_link_libraries(${target_name}
      PRIVATE
      GTest::gtest_main)

    if(ARG_LINK_LIBROCKY)
      target_link_libraries(${target_name}
        PRIVATE
        librocky)
    endif()

    target_compile_options(${target_name}
      PRIVATE
      -fsanitize=thread
      -fno-omit-frame-pointer)

    target_link_options(${target_name}
      PRIVATE
      -fsanitize=thread)

    gtest_discover_tests(${target_name})
  endfunction()

  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*_test.cc")

  foreach(test_path ${TEST_SOURCES})
    get_filename_component(test_name "${test_path}" NAME)

    if(test_name MATCHES "_lib_test\\.cc$")
      add_rocky_test("${test_path}" LINK_LIBROCKY)
    else()
      add_rocky_test("${test_path}")
    endif()
  endforeach()
endif()
