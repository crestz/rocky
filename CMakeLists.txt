cmake_minimum_required(VERSION 4.0.0)

project("rocky")

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ROCKY_BUILD_TESTS "Build Unit Tests" OFF)

if(${ROCKY_BUILD_TESTS})
  enable_testing()
endif()

include_directories("include")

add_subdirectory("src")

file(WRITE "${CMAKE_SOURCE_DIR}/.clangd"
  "CompileFlags:
  CompilationDatabase: \"${CMAKE_BINARY_DIR}\"
")

