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

# ---------- Build type & warnings ----------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g -O3 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
# set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra -fsanitize=address,undefined")
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O0 -fsanitize=address,undefined")
add_definitions(-DTIMERON)

# Root of your sources/headers
set(TRACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------- Package discovery ----------------
# IMPORTANT: pass your Spack view via -DCMAKE_PREFIX_PATH=<.../.spack-env/view>
find_package(MPI REQUIRED COMPONENTS C CXX)

# Prefer the parallel HDF5 (prevents mixing)
set(HDF5_PREFER_PARALLEL TRUE)
find_package(HDF5 REQUIRED COMPONENTS C)

# Boost serialization
find_package(Boost REQUIRED COMPONENTS serialization CONFIG)

# Threads (you link Threads::Threads below)
find_package(Threads REQUIRED)

# Adios for SST
find_package(ADIOS2 REQUIRED CONFIG)
# Pick an appropriate ADIOS2 C++ target (version / build dependent)
set(_adios2_cxx_target "")
if(TARGET adios2::cxx_mpi)
  set(_adios2_cxx_target adios2::cxx_mpi)
elseif(TARGET adios2::cxx11_mpi)
  set(_adios2_cxx_target adios2::cxx11_mpi)
elseif(TARGET adios2::cxx11)
  set(_adios2_cxx_target adios2::cxx11)
elseif(TARGET adios2::cxx)
  # fallback if some older build actually exports this
  set(_adios2_cxx_target adios2::cxx)
else()
  message(FATAL_ERROR
    "Could not find an ADIOS2 C++ target. Tried: "
    "adios2::cxx_mpi, adios2::cxx11_mpi, adios2::cxx11, adios2::cxx. "
    "Check that ADIOS2 was built with C++ bindings and that "
    "CMAKE_PREFIX_PATH / ADIOS2_DIR points to the right install.")
endif()

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

# ---------------- Collect Spack view libdirs for link-time -l resolution ----------------
set(_SPACK_LIBDIRS "")
foreach(_prefix ${CMAKE_PREFIX_PATH})
  foreach(_sfx lib lib64)
    if(EXISTS "${_prefix}/${_sfx}")
      list(APPEND _SPACK_LIBDIRS "${_prefix}/${_sfx}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _SPACK_LIBDIRS)

# ---------------- Your includes (only your headers) ----------------
include_directories(${TRACE_ROOT}/include)
include_directories(${TRACE_ROOT}/include/tracelib)
include_directories(${TRACE_ROOT}/include/comm_sirt)
include_directories(${TRACE_ROOT}/include/sirt)
if(_flatbuf_target STREQUAL "" AND FLATBUFFERS_INCLUDE_DIR)
  include_directories(${FLATBUFFERS_INCLUDE_DIR})
endif()

# ---------------- Libraries from your tree ----------------
add_library(trace_utils ${TRACE_ROOT}/src/tracelib/trace_utils.cc)
target_link_libraries(trace_utils
  PUBLIC
    Boost::serialization
    HDF5::HDF5
)

add_library(trace_h5io  ${TRACE_ROOT}/src/tracelib/trace_h5io.cc)
target_link_libraries(trace_h5io
  PUBLIC
    HDF5::HDF5
    MPI::MPI_C
    Boost::serialization
)

add_library(sst_stream ${TRACE_ROOT}/src/comm_sirt/sst_stream.cc)
target_link_libraries(sst_stream
  PUBLIC
    ${_adios2_cxx_target}
)

add_library(mofka_stream ${TRACE_ROOT}/src/comm_sirt/mofka_stream.cc)
# Make mofka PUBLIC so consumers inherit the dependency (prevents DSO-missing)
target_link_libraries(mofka_stream
  PUBLIC
    mofka::client
    ${_adios2_cxx_target}
)
if(BEDROCK_MODULE_API)
  target_link_libraries(mofka_stream PUBLIC ${BEDROCK_MODULE_API})
  get_filename_component(_BEDROCK_DIR "${BEDROCK_MODULE_API}" DIRECTORY)
  if(EXISTS "${_BEDROCK_DIR}")
    target_link_directories(mofka_stream PUBLIC "${_BEDROCK_DIR}")
  endif()
endif()
if(_SPACK_LIBDIRS)
  target_link_directories(mofka_stream PUBLIC ${_SPACK_LIBDIRS})
endif()

add_library(sirt ${TRACE_ROOT}/src/sirt/sirt.cc)
target_link_libraries(sirt
  PUBLIC
    trace_utils
    Boost::serialization
    HDF5::HDF5
    ${_adios2_cxx_target}
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
    sst_stream
    mofka::client            # <--- add direct link (belt & suspenders)
    MPI::MPI_CXX
    Threads::Threads
    HDF5::HDF5
    Boost::serialization
)

# Optional: keep linker from dropping DSOs that are only used indirectly
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Intel|NVHPC")
  target_link_options(sirt_stream PRIVATE -Wl,--no-as-needed)
endif()

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
  target_link_directories(sirt_stream PRIVATE ${_SPACK_LIBDIRS})
endif()

# --- RPATH so the binary can find Spack libs at runtime ---
# Collect libdirs we already computed
set(_RPATH_DIRS ${_SPACK_LIBDIRS})

# Also add margo’s directory if we can discover it from the imported target
get_target_property(_margo_loc mofka::client LOCATION)
if(NOT _margo_loc)
  # fall back: try to use the bedrock/margo dir we added above
  if(BEDROCK_MODULE_API)
    get_filename_component(_BEDROCK_DIR "${BEDROCK_MODULE_API}" DIRECTORY)
    list(APPEND _RPATH_DIRS "${_BEDROCK_DIR}")
  endif()
endif()
# Common Cray/Spack places (adjust if needed)
list(APPEND _RPATH_DIRS
  "/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view/lib"
  "/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view/lib64"
)

list(REMOVE_DUPLICATES _RPATH_DIRS)

# Use RPATH in the build tree and when installed
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_RPATH "${_RPATH_DIRS}")
set(CMAKE_INSTALL_RPATH "${_RPATH_DIRS}")

# Also set per-target in case wrappers ignore globals
set_target_properties(sirt_stream PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")
set_target_properties(mofka_stream PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")
set_target_properties(trace_utils  PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")
set_target_properties(trace_h5io   PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")
set_target_properties(sirt         PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")
set_target_properties(recon_engine PROPERTIES BUILD_RPATH "${_RPATH_DIRS}")

# ---------------- Status ----------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "TRACE_ROOT: ${TRACE_ROOT}")
message(STATUS "Flatbuffers target: ${_flatbuf_target}")
message(STATUS "VeloC link: ${_veloc_target}")
message(STATUS "Bedrock module api: ${BEDROCK_MODULE_API}")