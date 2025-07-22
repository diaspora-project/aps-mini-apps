find_package(SWIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)

add_library(trace_streamer SHARED ${CMAKE_CURRENT_LIST_DIR}/trace_streamer.c)
add_library(mock_data_acq SHARED ${CMAKE_CURRENT_LIST_DIR}/mock_data_acq.c)

include(${SWIG_USE_FILE})

swig_add_library(server
    TYPE SHARED
    LANGUAGE python
    SOURCES server.i server.c)

include_directories(
    ${Python3_INCLUDE_DIRS}
    ${Python3_NumPy_INCLUDE_DIRS}
    ${CMAKE_CURRENT_LIST_DIR})
#get_target_property(SERVER_LIB_PATH server LOCATION)
target_link_libraries(server trace_streamer mock_data_acq Python3::Python zmq)

#file(COPY ${CMAKE_CURRENT_LIST_DIR}/ModDistStreamPubDemo.py DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../python/streamer_dist)
#file(COPY ${CMAKE_BINARY_DIR}/python/common DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python)
#file(COPY ${CMAKE_BINARY_DIR}/python/streamer-dist/_server.so DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/streamer-dist/)
#message(STATUS "Current binary dir: ${CMAKE_CURRENT_BINARY_DIR}")
#message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
#message(STATUS "Current runtime output dir: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/streamer-dist/ModDistStreamPubDemo.py")
#message(STATUS "Current runtime output dir: ${CMAKE_CURRENT_SOURCE_DIR}/ModDistStreamPubDemo.py")

#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ModDistStreamPubDemo.py DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/streamer-dist/)
#file(COPY ${CMAKE_CURRENT_BINARY_DIR}/_server.so DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/streamer-dist/)
#file(COPY ${CMAKE_CURRENT_BINARY_DIR}/tracemq.py DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/streamer-dist/)