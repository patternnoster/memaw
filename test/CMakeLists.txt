cmake_minimum_required(VERSION 3.23)

add_executable(memaw_tests
  concepts_tests.cpp
  resource_tests.cpp
)
target_link_libraries(memaw_tests PRIVATE memaw gmock gtest_main)

# Show all warnings because we're pedantic (and also all and extra)
target_compile_options(memaw_tests PRIVATE
  $<IF:$<BOOL:${MSVC}>, /W3, -Wall -Wpedantic -Wextra>)

include(GoogleTest)
gtest_discover_tests(memaw_tests)
