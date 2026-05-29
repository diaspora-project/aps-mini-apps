include(GNUInstallDirs)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Pure-python content always goes under lib/, never lib64/ — Spack's
# `extends("python")` view linker only inspects <prefix>/lib/pythonX.Y/
# site-packages, and on RHEL/Rocky systems (e.g. Improv) GNUInstallDirs
# resolves CMAKE_INSTALL_LIBDIR to lib64, which would orphan the module.
set(APS_PY_SITE
    lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages)

# Build-tree copy so run-local-*.sh and the build-tree dev workflow work
# without `make install` (PYTHONPATH=build/python picks up tekapp from here).
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp
     DESTINATION ${CMAKE_BINARY_DIR}/python/)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp
        DESTINATION ${APS_PY_SITE}
        FILES_MATCHING PATTERN "*.py")

# fakapp mirrors tekapp's package layout. The FlatBuffers Python bindings
# (common/mona/tracedm/*.py) are not duplicated in source — they are
# copied from tekapp's common/ at configure time and re-rooted under
# fakapp.common.mona so fakapp doesn't import anything from tekapp.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fakapp
     DESTINATION ${CMAKE_BINARY_DIR}/python/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp/common/mona
     DESTINATION ${CMAKE_BINARY_DIR}/python/fakapp/common/)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/fakapp
        DESTINATION ${APS_PY_SITE}
        FILES_MATCHING PATTERN "*.py")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tekapp/common/mona
        DESTINATION ${APS_PY_SITE}/fakapp/common
        FILES_MATCHING PATTERN "*.py")

# Generate console-script launchers for both tekapp and fakapp:
# {tekapp,fakapp}-{daq,dist,denoiser}. Each is a tiny stub that imports
# its subpackage's main() and runs it.
foreach(_app tekapp fakapp)
    foreach(_comp daq dist denoiser)
        set(TEKAPP_ENTRY_MODULE "${_app}.streamer_${_comp}")
        set(_launcher ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_app}-${_comp})
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
endforeach()
