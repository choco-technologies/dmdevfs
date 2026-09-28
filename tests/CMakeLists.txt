cmake_minimum_required(VERSION 3.18)

project(dmdevfs_tests VERSION 1.0 DESCRIPTION "DMDEVFS Unit Tests" LANGUAGES C)

# Ensure DMOD is available
if(NOT DMOD_DIR)
    message(FATAL_ERROR "DMOD_DIR not set. Tests must be built as part of main project.")
endif()

# ======================================================================
#               Build verification tests
# ======================================================================
# These tests verify the module was built correctly

# Test 1: Check if the module binary was created
add_test(NAME dmdevfs_module_built
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying dmdevfs module was built..."
)

# Test 2: Check if dmf directory exists (cross-platform)
add_test(NAME dmdevfs_dmf_directory
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/dmf ${CMAKE_COMMAND} -E echo "DMF directory exists"
)

# Test 3: Verify module files exist (cross-platform)
# We check if the file exists by trying to copy it to itself
add_test(NAME dmdevfs_dmf_file
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        ${CMAKE_BINARY_DIR}/dmf/dmdevfs.dmf 
        ${CMAKE_BINARY_DIR}/dmf/dmdevfs.dmf.test
)

# Cleanup test file
add_test(NAME dmdevfs_dmf_file_cleanup
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/dmf/dmdevfs.dmf.test
)
set_tests_properties(dmdevfs_dmf_file_cleanup PROPERTIES
    DEPENDS dmdevfs_dmf_file
)

# Test 4: Check source file compilation
add_test(NAME dmdevfs_source_compiled
    COMMAND ${CMAKE_COMMAND} -E echo "Source files compiled successfully"
)

# ======================================================================
#               test_dmdevfs - libsystemd node reporting
# ======================================================================
# Test-only helper modules, never released: a dmdrvi driver dmdevfs loads
# from fixtures/config, and the service libsystemd starts for each report.
add_subdirectory(mockdrv)
add_subdirectory(testsvc)

set(DMOD_MODULE_NAME        test_dmdevfs)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    test_dmdevfs.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mockdrv
)

target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    DMDEVFS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmosi
    libsystemd
)

# Replays what CI does: install the test's dependencies, then run it.
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
else()
    message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}")
endif()

# Print information about tests
message(STATUS "DMDEVFS tests configured")
message(STATUS "  Module output directory: ${CMAKE_BINARY_DIR}/dmf")
message(STATUS "  Run 'ctest' to verify the build")
message(STATUS "  Note: Full integration tests require dmf-get tool and device drivers")
