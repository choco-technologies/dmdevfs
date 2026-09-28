# dmdevfs_testsvc - test-only Application started by libsystemd for the
# devices dmdevfs reports (fixtures/units/*.ini). Never released.
set(DMOD_MODULE_NAME        dmdevfs_testsvc)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    testsvc.c
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
)
