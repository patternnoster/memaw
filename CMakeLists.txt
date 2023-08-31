cmake_minimum_required(VERSION 3.23)

project(memaw LANGUAGES CXX)

option(MEMAW_BUILD_TESTS "Build tests for this library" OFF)

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(memaw INTERFACE)
target_compile_features(memaw INTERFACE cxx_std_20)
target_include_directories(memaw INTERFACE $<BUILD_INTERFACE:${INCLUDE_DIR}>)
target_link_libraries(memaw INTERFACE nupp)

add_subdirectory(extern)

if(MEMAW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
