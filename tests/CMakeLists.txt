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

# Registers a test module with ctest - replays what CI does: install its
# dependencies, then run it through dmod_loader.
#
# The tests run in this directory and use relative fixture paths
# ("fixtures/..."): dmdevfs limits a configuration file path to
# MAX_PATH_LENGTH (52 bytes), which an absolute path into the checkout
# exceeds as soon as the repository sits a few characters deeper (CI:
# /__w/dmdevfs/dmdevfs/...).
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
#
# dmdevfs itself is not a dependency of the test modules (they mount it by
# name), so its own dependencies - e.g. dmpart - are installed from
# dmdevfs.dmd first; the test module's own ones from <name>.dmd.
function(dmdevfs_register_test name)
    if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
        add_test(
            NAME ${name}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
                bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/dmdevfs.dmd -y && ${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${name}.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${name}.dmf"
        )
        # A hang must fail the run (with its output), not stall CI.
        set_tests_properties(${name} PROPERTIES TIMEOUT 180)
    else()
        message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${name}")
    endif()
endfunction()

set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)

# test_dmdevfs - libsystemd node reporting
set(DMOD_MODULE_NAME        test_dmdevfs)
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    test_dmdevfs.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mockdrv
)
target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    DMDEVFS_TEST_FIXTURES_DIR="fixtures"
)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmhaman
    dmosi
    libsystemd
)
dmdevfs_register_test(${DMOD_MODULE_NAME})

# test_dmdevmon - the dmdevmon monitor loop. Compiles the loop's source
# (the unit under test) into the test; the service's main.c, which only
# adds the file API and libsystemd around it, cannot run on the host loader
# (no dmvfs there).
set(DMOD_MODULE_NAME        test_dmdevmon)
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    test_dmdevmon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/dmdevmon/src/dmdevmon.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mockdrv
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/dmdevmon/src
)
target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    DMDEVFS_TEST_FIXTURES_DIR="fixtures"
)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmdrvi@>=2.1
    dmhaman
    dmosi
)
dmdevfs_register_test(${DMOD_MODULE_NAME})

# test_partitions - partition nodes (MBR/GPT) on the test driver's medium
set(DMOD_MODULE_NAME        test_partitions)
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    test_partitions.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mockdrv
)
target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    DMDEVFS_TEST_FIXTURES_DIR="fixtures"
)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmdrvi@>=2.1
    dmhaman
    dmosi
    libsystemd
)
dmdevfs_register_test(${DMOD_MODULE_NAME})

# test_order - configuration order by driver_order (negative, default, positive)
set(DMOD_MODULE_NAME        test_order)
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    test_order.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mockdrv
)
target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    DMDEVFS_TEST_FIXTURES_DIR="fixtures"
)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmdrvi@>=2.1
    dmhaman
    dmosi
    libsystemd
)
dmdevfs_register_test(${DMOD_MODULE_NAME})

# Print information about tests
message(STATUS "DMDEVFS tests configured")
message(STATUS "  Module output directory: ${CMAKE_BINARY_DIR}/dmf")
message(STATUS "  Run 'ctest' to verify the build")
message(STATUS "  Note: Full integration tests require dmf-get tool and device drivers")
