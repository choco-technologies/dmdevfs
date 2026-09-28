# =====================================================================
#               dmdevmon - generic monitor service
# =====================================================================
# Drives any node implementing the dmdrvi monitor contract
# (DMDRVI_IOCTL_MONITOR_*), started per node by libsystemd - see README.md.
set(DMOD_MODULE_NAME        dmdevmon)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmdevmon.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
    src/dmdevmon.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.1
    dmhaman
    dmosi
    libsystemd
)
