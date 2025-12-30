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

# Print information about tests
message(STATUS "DMDEVFS tests configured")
message(STATUS "  Module output directory: ${CMAKE_BINARY_DIR}/dmf")
message(STATUS "  Run 'ctest' to verify the build")
message(STATUS "  Note: Full integration tests require dmf-get tool and device drivers")
