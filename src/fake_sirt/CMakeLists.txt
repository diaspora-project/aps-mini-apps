# fake-sirt: protocol-equivalent of sirt_stream with no SIRT engine and
# no tracelib deps. Uses MPI + diaspora-stream-api + HDF5 (rank-0 serial
# write) + spdlog/fmt + nlohmann/json + TCLAP.
#
# All find_package() calls are re-issued here because src/sirt/CMakeLists.txt
# scopes its imported targets (MPI::MPI_CXX, hdf5::hdf5, fmt::*) to its own
# subdirectory.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(MPI REQUIRED)
find_package(HDF5 REQUIRED)
find_package(Threads REQUIRED)
find_package(diaspora-stream-api REQUIRED)
find_package(fmt REQUIRED)

add_executable(fake_sirt
    ${CMAKE_CURRENT_LIST_DIR}/fake_sirt_main.cc
    ${CMAKE_CURRENT_LIST_DIR}/fake_diaspora_stream.cc)

target_include_directories(fake_sirt PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${Trace_SOURCE_DIR}/include
    ${HDF5_INCLUDE_DIRS}
    ${MPI_INCLUDE_PATH})

target_link_libraries(fake_sirt PRIVATE
    diaspora-stream-api
    MPI::MPI_CXX
    hdf5::hdf5
    Threads::Threads
    fmt::fmt-header-only)

install(TARGETS fake_sirt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# fakapp-sirt symlink, mirroring tekapp's tekapp-sirt -> sirt_stream pattern.
add_custom_command(TARGET fake_sirt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            fake_sirt ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fakapp-sirt
    VERBATIM)

install(CODE "
    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
                    fake_sirt
                    \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/fakapp-sirt)
")
