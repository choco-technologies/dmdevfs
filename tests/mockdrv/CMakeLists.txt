# dmdevfs_mockdrv - test-only dmdrvi driver loaded by dmdevfs from
# fixtures/config (see test_dmdevfs.c). Never released.
set(DMOD_MODULE_NAME        dmdevfs_mockdrv)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_MAL_IMPLS          )

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    mockdrv.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.1
    dmini
)
