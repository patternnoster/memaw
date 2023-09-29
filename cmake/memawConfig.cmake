cmake_minimum_required(VERSION 3.5)

include(CMakeFindDependencyMacro)
find_dependency(nupp)

include(${CMAKE_CURRENT_LIST_DIR}/memawConfigTargets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/memawConfigVersion.cmake)
