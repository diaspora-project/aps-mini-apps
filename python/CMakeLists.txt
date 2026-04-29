# Copy Python scripts and shared utilities to the build directory at configure
# time so they are available alongside the compiled binaries without any manual
# post-build steps.

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/common
     DESTINATION ${CMAKE_BINARY_DIR}/python/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/streamer-daq/DAQStream.py
     DESTINATION ${CMAKE_BINARY_DIR}/python/streamer-daq/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/streamer-denoiser/denoiser.py
     DESTINATION ${CMAKE_BINARY_DIR}/python/streamer-denoiser/)

add_subdirectory(streamer-dist)
