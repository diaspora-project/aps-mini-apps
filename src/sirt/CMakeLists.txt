set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# setup cache variables for ccmake
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release
         CACHE STRING "Choose the type of build." FORCE)
    set_property (CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                  "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()
set (CMAKE_PREFIX_PATH "" CACHE STRING "External dependencies path")
set (BUILD_SHARED_LIBS "ON" CACHE BOOL "Build a shared library")
# Enable debugging symbols and disable optimization
set(CMAKE_BUILD_TYPE Debug)

# Optionally, add more flags for warnings and debugging:
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wno-unused-but-set-variable -Wno-unused-variable")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
add_definitions(-DTIMERON)

find_package(Flatbuffers REQUIRED)
include_directories(${FLATBUFFERS_INCLUDE_DIR})

find_package(MPI REQUIRED)
include_directories(${MPI_INCLUDE_PATH})

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

#find_package(ZeroMQ)
find_package(HDF5)

if(NOT HDF5_FOUND OR NOT HDF5_IS_PARALLEL)
    message(FATAL_ERROR "HDF5 library not compiled with parallel support.")
endif()

# find mofka
find_package (mofka REQUIRED)

include_directories(${Trace_SOURCE_DIR}/include)
include_directories(${Trace_SOURCE_DIR}/include/tracelib)
include_directories(${HDF5_INCLUDE_DIRS})

include_directories(${Trace_SOURCE_DIR}/include)
include_directories(${Trace_SOURCE_DIR}/include/mofka_sirt)

add_library(trace_utils ${Trace_SOURCE_DIR}/src/tracelib/trace_utils.cc)
add_library(trace_h5io ${Trace_SOURCE_DIR}/src/tracelib/trace_h5io.cc)

add_library(mofka_stream ${Trace_SOURCE_DIR}/src/mofka_sirt/mofka_stream.cc)
target_link_libraries(mofka_stream PRIVATE mofka::client)
add_library(sirt ${CMAKE_CURRENT_LIST_DIR}/sirt.cc)

add_executable(sirt_stream sirt_stream_main.cc)
target_link_libraries(sirt_stream mofka_stream sirt trace_utils trace_h5io MPI::MPI_CXX hdf5::hdf5 Threads::Threads mofka::client)
#target_include_directories(sirt_stream PRIVATE ${HDF5_INCLUDE_DIRS})
