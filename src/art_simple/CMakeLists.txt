cmake_minimum_required(VERSION 3.10)

# Set the project name
project(art_simple_main)

# Find the HDF5 library
find_package(HDF5 REQUIRED)

# Add an executable
add_executable(art_simple_main main.cc art_simple.cc)

# Link the HDF5 library
target_link_libraries(art_simple_main HDF5::HDF5)