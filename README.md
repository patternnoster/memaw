## The memaw library

C++20 tools for working with memory. Still work in progress, see [documentation](doc/index.md) for a list of features implemented so far.

### Installation

The library is header-only. To install the headers along with the CMake configuration file (for `find_package`) one can use the standard CMake procedure:
```sh
cmake $SOURCE_PATH
cmake --install . --prefix=$INSTALL_PATH
```
The procedure will also install the header-only [nupp](https://github.com/patternnoster/nupp) library as a dependency.

### Running the tests

Use the `MEMAW_BUILD_TESTS=ON` CMake option to build the library tests on your system (requires the googletest submodule). One can run the following commands (in the source directory) to build and run the tests:
```sh
mkdir build && cd build
cmake .. -DMEMAW_BUILD_TESTS=ON
cmake --build .
ctest
```
