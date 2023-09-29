cmake_minimum_required(VERSION 3.23)

find_package(Git QUIET REQUIRED)

function(get_git_version VAR)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --always --broken=-b --dirty=-d
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  string(REGEX REPLACE "^v\\.?" "" VERSION ${GIT_VERSION})

  message(STATUS "The version is ${VERSION}")
  set(${VAR} ${VERSION} PARENT_SCOPE)
endfunction()
