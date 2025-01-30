# Minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(TomographicReconstruction VERSION 1.0)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set optimization flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Find the HDF5 library
find_package(HDF5 REQUIRED COMPONENTS C CXX)

# Include directories
include_directories(
    ${HDF5_INCLUDE_DIRS}
)

# Add the executable
add_executable(sirt_refactored sirt_refactored.cc)

# Link the HDF5 libraries
target_link_libraries(sirt_refactored ${HDF5_LIBRARIES})
