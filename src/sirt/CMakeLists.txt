cmake_minimum_required(VERSION 3.20)
project(sirt_stream_project LANGUAGES C CXX)

# ---- Language / policies ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep each policy on its own line (fixes your newline parse error)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW) # use BoostConfig.cmake when available
endif()

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW) # suppress BOOST_ROOT warning noise
endif()

# ---- Build types & flags ----
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# You asked for Debug; keep it explicit (remove this line if you want Release)
set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)

# Keep warnings but avoid -Werror during package feature checks
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
# If you really want -Werror, add it to *your targets only* after find_package:
# set_property(TARGET <your_targets> APPEND_STRING PROPERTY COMPILE_OPTIONS " -Werror")
add_definitions(-DTIMERON)

# Root of your sources/headers (replace if your layout differs)
set(TRACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ---- Dependencies (use imported targets from your Spack view) ----
# IMPORTANT: do NOT use link_directories; rely on CMAKE_PREFIX_PATH instead.

find_package(Flatbuffers REQUIRED)            # Flatbuffers::flatbuffers
find_package(MPI REQUIRED COMPONENTS C CXX)   # MPI::MPI_C, MPI::MPI_CXX
find_package(Threads REQUIRED)                # Threads::Threads
find_package(HDF5 REQUIRED)                   # hdf5::hdf5 (Spack config exports this)
find_package(Boost REQUIRED COMPONENTS serialization CONFIG)  # Boost::serialization

# VeloC: prefer a package config target if Spack provides one; otherwise fall back to raw lib
find_package(veloc QUIET)
if(TARGET veloc::veloc)
  set(VELOC_CLIENT_TARGET veloc::veloc)
else()
  find_library(VELOC_CLIENT_LIB NAMES veloc-client REQUIRED)
endif()

# Mofka: your earlier code linked mofka::client
find_package(mofka REQUIRED)                  # mofka::client

# Ensure parallel HDF5 if that’s a hard requirement
if(DEFINED HDF5_IS_PARALLEL AND NOT HDF5_IS_PARALLEL)
  message(FATAL_ERROR "HDF5 library was found but is NOT compiled with parallel support.")
endif()

# ---- Include dirs for your *own* headers only ----
# (Imported package targets propagate their own include paths)
include_directories(${TRACE_ROOT}/include)
include_directories(${TRACE_ROOT}/include/tracelib)
include_directories(${TRACE_ROOT}/include/mofka_sirt)
include_directories(${TRACE_ROOT}/include/sirt)

# ---- Libraries from your tree ----
add_library(trace_utils ${TRACE_ROOT}/src/tracelib/trace_utils.cc)

add_library(trace_h5io  ${TRACE_ROOT}/src/tracelib/trace_h5io.cc)
target_link_libraries(trace_h5io PUBLIC hdf5::hdf5 MPI::MPI_C)

add_library(mofka_stream ${TRACE_ROOT}/src/mofka_sirt/mofka_stream.cc)
target_link_libraries(mofka_stream PRIVATE mofka::client)

add_library(sirt ${CMAKE_CURRENT_LIST_DIR}/sirt.cc)

add_library(recon_engine recon_engine.cc)
target_link_libraries(recon_engine PUBLIC sirt mofka_stream trace_utils trace_h5io)

# ---- Executable ----
add_executable(sirt_stream ${TRACE_ROOT}/src/sirt/sirt_stream_main.cc)

# Link order: propagate your libs first; imported targets last
target_link_libraries(sirt_stream
  PRIVATE
    recon_engine
    trace_utils
    trace_h5io
    sirt
    mofka_stream
    Boost::serialization
    Flatbuffers::flatbuffers
    MPI::MPI_CXX
    Threads::Threads
    hdf5::hdf5
    mofka::client
)

# If VeloC only available as a raw library, add it here
if(DEFINED VELOC_CLIENT_TARGET)
  target_link_libraries(sirt_stream PRIVATE ${VELOC_CLIENT_TARGET})
elseif(DEFINED VELOC_CLIENT_LIB)
  target_link_libraries(sirt_stream PRIVATE ${VELOC_CLIENT_LIB})
endif()

# ---- Helpful status ----
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using TRACE_ROOT=${TRACE_ROOT}")