# =====================================================================
#       devscan - the driver configuration directory of a mount point
# =====================================================================
# A one-shot service: started by a libsystemd rule (configs/devscan.rules) when a file system is mounted, it asks a
# dmdevfs mount (ioctl DMDEVFS_IOCTL_ADD_CONFIG_DIR) to configure <mount point>/cfg/drivers and ends; started again
# by the rule's "remove" key before the file system is unmounted, it asks for them to be dropped. See README.md.
set(DMOD_MODULE_NAME        devscan)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
# Its own stack: it only opens a file and calls an ioctl, but the thread that mounts (automount's) is far too small
# to do even that inline
set(DMOD_STACK_SIZE         2048)
# Above the services (0): a mount's `remove` unit is waited for by the unmount, and on a busy system (the car HMI) a
# priority-0 process gets the CPU only in the gaps - it took the 20 s the unmount waits for it at most. It does
# a few file operations and ends.
set(DMOD_PRIORITY           3)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/devscan.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

# dmdevfs.h - the ioctl commands; devscan does not link dmdevfs, it talks to whichever one is mounted
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
)
