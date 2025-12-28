# =====================================================================
#               DMOD Driver File System Module
# =====================================================================
cmake_minimum_required(VERSION 3.18)

# ======================================================================
#               For VS Code
# ======================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ======================================================================
#               DMOD DFS
# ======================================================================
# Allow version to be passed as a parameter, default to 0.1
if(NOT DEFINED DMOD_MODULE_VERSION)
    set(DMOD_MODULE_VERSION "0.1" CACHE STRING "DMOD module version")
endif()
set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")

# ======================================================================
#               Fetch DMOD repository
# ======================================================================
include(FetchContent)
FetchContent_Declare(
    dmod
    GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
    GIT_TAG        develop
)

# ======================================================================
#               DMOD Configuration
# ======================================================================
set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")

FetchContent_MakeAvailable(dmod)

project(dmdevfs
    VERSION ${DMOD_MODULE_VERSION}
    DESCRIPTION "DMOD Driver File System"
    LANGUAGES C CXX)

# ======================================================================
#               Import dmod functions and macros
# ======================================================================
set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory")
include(${DMOD_DIR}/paths.cmake)
dmod_setup_external_module()

# ======================================================================
#               DMDEVFS Module Configuration
# ======================================================================
# Name of the module 
set(DMOD_MODULE_NAME        dmdevfs)

# Version is already set above and used in project()
# No need to set it again here

# Author (should be string)
set(DMOD_AUTHOR_NAME        Patryk Kubiak)

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmdevfs.c
)

# Link to other DMOD modules (will be downloaded on build time)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmfsi
    dmdrvi
    dmini
    dmlist
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)