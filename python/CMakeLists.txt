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
