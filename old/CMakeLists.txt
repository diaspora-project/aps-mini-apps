cmake_minimum_required(VERSION 3.20.0)
project(Trace VERSION 0.1.0.0)

# Check the compiler flags etc.
set(CMAKE_VERBOSE_MAKEFILE on)
# Put all executables into build/bin folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src)
add_subdirectory(python)
