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

# ---------- Build type & warnings (avoid -Werror during detection) ----------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
add_definitions(-DTIMERON)

# Root of your sources/headers
set(TRACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------- Package discovery ----------------
# IMPORTANT: pass your Spack view via -DCMAKE_PREFIX_PATH=<.../.spack-env/view>
find_package(MPI REQUIRED COMPONENTS C CXX)

# Prefer the parallel HDF5 (prevents mixing)
set(HDF5_PREFER_PARALLEL TRUE)
find_package(HDF5 REQUIRED COMPONENTS C)
if(DEFINED HDF5_IS_PARALLEL AND NOT HDF5_IS_PARALLEL)
  message(FATAL_ERROR "Found HDF5 but it is NOT parallel. Load cray-hdf5-parallel or adjust HDF5_DIR.")
endif()

# Boost serialization (use CONFIG if available)
find_package(Boost REQUIRED COMPONENTS serialization CONFIG)

# Flatbuffers: support multiple target names, fallback to headers
set(_flatbuf_target "")
find_package(Flatbuffers QUIET CONFIG)
if(TARGET Flatbuffers::flatbuffers)
  set(_flatbuf_target Flatbuffers::flatbuffers)
elseif(TARGET flatbuffers::flatbuffers)
  set(_flatbuf_target flatbuffers::flatbuffers)
else()
  find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h)
  if(NOT FLATBUFFERS_INCLUDE_DIR)
    message(FATAL_ERROR "Flatbuffers not found: add it to your Spack view and pass CMAKE_PREFIX_PATH.")
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
    message(WARNING "VeloC not found as a CMake target or library; continuing without it.")
  endif()
endif()

# Mofka client
find_package(mofka REQUIRED CONFIG)  # provides mofka::client

# Bedrock module API (needed by mofka at link-time on your site)
find_library(BEDROCK_MODULE_API NAMES bedrock-module-api PATH_SUFFIXES lib lib64)

# ---------------- Link directories for Spack view (to satisfy -l flags) ----------------
# Some packages inject -l<name>; add <view>/lib and <view>/lib64 so the linker finds them.
# CMAKE_PREFIX_PATH can be a list; iterate and add both lib and lib64.
foreach(_prefix ${CMAKE_PREFIX_PATH})
  foreach(_sfx lib lib64)
    if(EXISTS "${_prefix}/${_sfx}")
      # apply to targets that carry many third-party libs
      # (we avoid global link_directories() to keep scope tight)
      list(APPEND _SPACK_LIBDIRS "${_prefix}/${_sfx}")
    endif()
  endforeach()
endforeach()

# ---------------- Your includes (only your headers) ----------------
include_directories(${TRACE_ROOT}/include)
include_directories(${TRACE_ROOT}/include/tracelib)
include_directories(${TRACE_ROOT}/include/mofka_sirt)
include_directories(${TRACE_ROOT}/include/sirt)

# If Flatbuffers only gave us includes, add them
if(_flatbuf_target STREQUAL "")
  include_directories(${FLATBUFFERS_INCLUDE_DIR})
endif()

# ---------------- Libraries from your tree ----------------
add_library(trace_utils ${TRACE_ROOT}/src/tracelib/trace_utils.cc)
# These headers include Boost and (via shared headers) HDF5 → link PUBLIC so includes propagate
target_link_libraries(trace_utils
  PUBLIC
    Boost::serialization
    hdf5::hdf5
)

add_library(trace_h5io  ${TRACE_ROOT}/src/tracelib/trace_h5io.cc)
target_link_libraries(trace_h5io
  PUBLIC
    hdf5::hdf5
    MPI::MPI_C
    Boost::serialization
)

add_library(mofka_stream ${TRACE_ROOT}/src/mofka_sirt/mofka_stream.cc)
target_link_libraries(mofka_stream
  PRIVATE
    mofka::client
)
# Bedrock lib may also be required as a direct link on your platform
if(BEDROCK_MODULE_API)
  target_link_libraries(mofka_stream PRIVATE ${BEDROCK_MODULE_API})
  # ensure its directory is a link search path (to satisfy any -lbedrock-module-api flags)
  get_filename_component(_BEDROCK_DIR "${BEDROCK_MODULE_API}" DIRECTORY)
  if(EXISTS "${_BEDROCK_DIR}")
    target_link_directories(mofka_stream PRIVATE "${_BEDROCK_DIR}")
  endif()
endif()

add_library(sirt ${TRACE_ROOT}/src/sirt/sirt.cc)
target_link_libraries(sirt
  PUBLIC
    Boost::serialization
    hdf5::hdf5
)

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

# Add Spack view libdirs for link-time -l resolution
if(_SPACK_LIBDIRS)
  list(REMOVE_DUPLICATES _SPACK_LIBDIRS)
  target_link_directories(sirt_stream PRIVATE ${_SPACK_LIBDIRS})
  target_link_directories(mofka_stream PRIVATE ${_SPACK_LIBDIRS})
endif()

# ---------------- Status ----------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "TRACE_ROOT: ${TRACE_ROOT}")
message(STATUS "Flatbuffers target: ${_flatbuf_target}")
message(STATUS "VeloC link: ${_veloc_target}")
message(STATUS "Bedrock module api: ${BEDROCK_MODULE_API}")