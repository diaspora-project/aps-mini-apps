set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
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

include_directories(${Trace_SOURCE_DIR}/include)
include_directories(${Trace_SOURCE_DIR}/include/tracelib)
include_directories(${HDF5_INCLUDE_DIRS})

add_library(trace_stream ${Trace_SOURCE_DIR}/src/tracelib/trace_stream.cc)
add_library(trace_mq ${Trace_SOURCE_DIR}/src/tracelib/trace_mq.cc)
add_library(trace_utils ${Trace_SOURCE_DIR}/src/tracelib/trace_utils.cc)
add_library(trace_h5io ${Trace_SOURCE_DIR}/src/tracelib/trace_h5io.cc)
add_library(sirt ${CMAKE_CURRENT_LIST_DIR}/sirt.cc)


add_executable(sirt_stream sirt_stream_main.cc)
target_link_libraries(sirt_stream trace_stream trace_mq sirt trace_utils trace_h5io zmq MPI::MPI_CXX hdf5::hdf5 Threads::Threads)
#target_include_directories(sirt_stream PRIVATE ${HDF5_INCLUDE_DIRS})