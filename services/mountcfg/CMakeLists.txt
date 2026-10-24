# =====================================================================
#       mountcfg - configuration directories of mount points
# =====================================================================
# Follows mounts (dmvfs's mount listener) and loads the configuration
# directory (cfg/) of every mount point into dmdevfs (cfg/drivers) and
# libsystemd (cfg/services, cfg/rules) - see README.md. A Library module,
# started by its type=library unit (configs/mountcfg.ini).
set(DMOD_MODULE_NAME        mountcfg)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/mountcfg.dmr)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/mountcfg.c
)

# dmdevfs_cfg.h and the dmdevfs_defs.h it includes - dmdevfs is built by this
# repository's CMakeLists.txt
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# dmvfs: dmvfs_add_mount_listener() - dmvfs is built into the firmware, only
# its header is needed (dmvfs.h includes dmfsi.h, hence dmfsi). It makes
# mountcfg a module of the target only: the host loader has no dmvfs.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmvfs
    dmfsi
    dmosi
    dmlist
    libsystemd
)
