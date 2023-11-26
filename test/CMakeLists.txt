cmake_minimum_required(VERSION 3.23)

add_executable(memaw_tests
  cache_resource_tests.cpp
  chain_resource_tests.cpp
  os_resource_tests.cpp
  pages_resource_tests.cpp
  resource_common_tests.cpp
)
target_link_libraries(memaw_tests PRIVATE memaw gmock gtest_main)

# Show all warnings because we're pedantic (and also all and extra)
# Also enable exceptions explicitly on MSVC-like compilers on Windows
target_compile_options(memaw_tests PRIVATE
  $<IF:$<BOOL:${MSVC}>, /W3 /EHsc, -Wall -Wpedantic -Wextra>)

include(GoogleTest)
gtest_discover_tests(memaw_tests)
