# =====================================================================
#       mountcfg - the configuration directory of a mount point
# =====================================================================
# A one-shot service: started by a libsystemd rule (configs/mountcfg.rules) when a file system is mounted, it loads
# <mount point>/cfg into dmdevfs (cfg/drivers) and libsystemd (cfg/services, cfg/rules) and ends; started again by
# the rule's "remove" key before the file system is unmounted, it unloads it. See README.md.
set(DMOD_MODULE_NAME        mountcfg)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
# Its own stack - loading a directory parses ini files and starts units, which loads their modules; the thread that
# mounts (automount's) is far too small for that
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/mountcfg.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

# dmdevfs_cfg.h and the dmdevfs_defs.h it includes - dmdevfs is built by this repository's CMakeLists.txt
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    libsystemd
)
