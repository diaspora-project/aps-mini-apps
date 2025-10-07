cmake_minimum_required(VERSION 3.20)
project(sirt_stream_project LANGUAGES C CXX)

# ---------------- Basics / Policies ----------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW) # prefer BoostConfig.cmake
endif()
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW) # ignore BOOST_ROOT warnings
endif()

# ---------- Build type & warnings (no -Werror during detection) ----------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
add_definitions(-DTIMERON)

# Your tree root (adjust if needed)
set(TRACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------- Package discovery ----------------
# Rely on CMAKE_PREFIX_PATH (pass your Spack view on the cmake command line)
# Do NOT use link_directories.

# MPI via Cray wrappers
find_package(MPI REQUIRED COMPONENTS C CXX)

# HDF5: prefer the Cray parallel one; bail if not parallel
find_package(HDF5 REQUIRED COMPONENTS C)  # C interface sufficient for your code
if(DEFINED HDF5_IS_PARALLEL AND NOT HDF5_IS_PARALLEL)
  message(FATAL_ERROR "Found HDF5 but it is NOT parallel. Load cray-hdf5-parallel or point HDF5_DIR to a parallel build.")
endif()

# Boost serialization (CONFIG mode if available, otherwise MODULE)
# We'll link Boost::serialization so headers propagate to libraries.
find_package(Boost REQUIRED COMPONENTS serialization CONFIG)

# Flatbuffers: target name varies by distro/tooling. Handle both.
# Try config target names first, fall back to legacy include var.
set(_flatbuf_target "")
find_package(Flatbuffers QUIET CONFIG)
if(TARGET Flatbuffers::flatbuffers)
  set(_flatbuf_target Flatbuffers::flatbuffers)
elseif(TARGET flatbuffers::flatbuffers)
  set(_flatbuf_target flatbuffers::flatbuffers)
else()
  # Legacy FindFlatBuffers.cmake or none at all: just get includes if present
  find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h)
  if(NOT FLATBUFFERS_INCLUDE_DIR)
    message(FATAL_ERROR "Flatbuffers not found: no target and no include dir detected. Add flatbuffers to your Spack view and pass CMAKE_PREFIX_PATH.")
  endif()
endif()

# VeloC: prefer exported target, else raw library
set(_veloc_target "")
find_package(veloc QUIET CONFIG)
if(TARGET veloc::veloc)
  set(_veloc_target veloc::veloc)
else()
  find_library(VELOC_CLIENT_LIB NAMES veloc-client)
  if(VELOC_CLIENT_LIB)
    set(_veloc_target "${VELOC_CLIENT_LIB}")
  else()
    message(WARNING "VeloC not found as a CMake target or library; continuing without it. (If required, install + expose veloc in your view.)")
  endif()
endif()

# Mofka (client target expected)
find_package(mofka REQUIRED CONFIG) # provides mofka::client (per your usage)

# Bedrock module API: mofka may pull this; locate explicitly if not auto-provided
find_library(BEDROCK_MODULE_API NAMES bedrock-module-api PATH_SUFFIXES lib lib64)
if(NOT BEDROCK_MODULE_API)
  message(WARNING "bedrock-module-api not found by linker. If linking fails, set BEDROCK_MODULE_API path or ensure it is in your Spack view.")
endif()

# Threads
find_package(Threads REQUIRED)

# ---------------- Your includes (only your own headers) ----------------
include_directories(${TRACE_ROOT}/include)
include_directories(${TRACE_ROOT}/include/tracelib)
include_directories(${TRACE_ROOT}/include/mofka_sirt)
include_directories(${TRACE_ROOT}/include/sirt)

# If we only found Flatbuffers includes (no target), add them globally now.
if(NOT _flatbuf_target STREQUAL "")
  # nothing to do; targets propagate includes
else()
  include_directories(${FLATBUFFERS_INCLUDE_DIR})
endif()

# ---------------- Libraries from your tree ----------------
add_library(trace_utils ${TRACE_ROOT}/src/tracelib/trace_utils.cc)
# Needs Boost headers if they appear in public headers used elsewhere
target_link_libraries(trace_utils
  PUBLIC
    Boost::serialization
)

add_library(trace_h5io  ${TRACE_ROOT}/src/tracelib/trace_h5io.cc)
target_link_libraries(trace_h5io
  PUBLIC
    hdf5::hdf5         # ensures HDF5 includes/libs propagate
    MPI::MPI_C         # used in this source and/or headers
    Boost::serialization
)

add_library(mofka_stream ${TRACE_ROOT}/src/mofka_sirt/mofka_stream.cc)
target_link_libraries(mofka_stream
  PRIVATE
    mofka::client
)
# bedrock-module-api may be a hidden dependency of mofka on your site
if(BEDROCK_MODULE_API)
  target_link_libraries(mofka_stream PRIVATE ${BEDROCK_MODULE_API})
endif()

add_library(sirt ${TRACE_ROOT}/src/sirt/sirt.cc)
# If sirt’s public headers include Boost serialization, link it here too
target_link_libraries(sirt PUBLIC Boost::serialization)

add_library(recon_engine ${TRACE_ROOT}/src/sirt/recon_engine.cc)
target_link_libraries(recon_engine
  PUBLIC
    sirt
    mofka_stream
    trace_utils
    trace_h5io
)

# ---------------- Executable ----------------
add_executable(sirt_stream ${TRACE_ROOT}/src/sirt/sirt_stream_main.cc)
target_link_libraries(sirt_stream
  PRIVATE
    recon_engine
    trace_utils
    trace_h5io
    sirt
    mofka_stream
    MPI::MPI_CXX
    Threads::Threads
    hdf5::hdf5
    Boost::serialization
)

# Flatbuffers link (if we have a target)
if(NOT _flatbuf_target STREQUAL "")
  target_link_libraries(sirt_stream PRIVATE ${_flatbuf_target})
endif()

# VeloC link (if present)
if(NOT _veloc_target STREQUAL "")
  target_link_libraries(sirt_stream PRIVATE ${_veloc_target})
endif()

# ---------------- Status ----------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "TRACE_ROOT: ${TRACE_ROOT}")
message(STATUS "Flatbuffers target: ${_flatbuf_target}")
message(STATUS "VeloC link: ${_veloc_target}")
message(STATUS "Bedrock module api: ${BEDROCK_MODULE_API}")