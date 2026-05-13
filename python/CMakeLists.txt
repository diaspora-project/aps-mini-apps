include(GNUInstallDirs)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(APS_PY_SITE
    ${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages)

# Build-tree copy so run-local-*.sh and the build-tree dev workflow work
# without `make install` (PYTHONPATH=build/python picks up tekapp from here).
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp
     DESTINATION ${CMAKE_BINARY_DIR}/python/)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp
        DESTINATION ${APS_PY_SITE}
        FILES_MATCHING PATTERN "*.py")

# Generate console-script launchers: tekapp-{daq,dist,denoiser}.
# Each is a tiny Python stub that imports its subpackage's main() and runs it.
foreach(_comp daq dist denoiser)
    set(TEKAPP_ENTRY_MODULE "tekapp.streamer_${_comp}")
    set(_launcher ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tekapp-${_comp})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher.py.in
        ${_launcher}
        @ONLY)
    file(CHMOD ${_launcher}
         PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE
                     WORLD_READ WORLD_EXECUTE)
    install(PROGRAMS ${_launcher}
            DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()
