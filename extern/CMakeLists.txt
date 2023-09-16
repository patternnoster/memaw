cmake_minimum_required(VERSION 3.23)

add_subdirectory(nupp)

if(MEMAW_BUILD_TESTS)
  option(INSTALL_GTEST "" OFF)
  add_subdirectory(googletest)
endif()
