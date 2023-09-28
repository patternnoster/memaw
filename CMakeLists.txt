cmake_minimum_required(VERSION 3.23)

project(memaw LANGUAGES CXX)

option(MEMAW_BUILD_TESTS "Build tests for this library" OFF)

include(GNUInstallDirs)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(memaw INTERFACE)
target_compile_features(memaw INTERFACE cxx_std_20)
target_include_directories(memaw INTERFACE
  $<BUILD_INTERFACE:${INCLUDE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(memaw INTERFACE nupp)

add_subdirectory(extern)

if(MEMAW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

install(TARGETS memaw EXPORT memaw)
install(DIRECTORY ${INCLUDE_DIR}/memaw DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/memaw)
install(EXPORT memaw
  FILE memawConfigTargets.cmake
  DESTINATION ${INSTALL_CMAKEDIR})
install(FILES cmake/memawConfig.cmake DESTINATION ${INSTALL_CMAKEDIR})
