if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is not set")
endif()

file(READ "${SOURCE_FILE}" DMDEVFS_SOURCE)

string(FIND "${DMDEVFS_SOURCE}" "alt_driver->dev_num.alt_name" LEGACY_ALT_NAME_USAGE_POS)
if(LEGACY_ALT_NAME_USAGE_POS GREATER -1)
    message(FATAL_ERROR "Legacy alt name listing path found: alt_driver->dev_num.alt_name")
endif()

string(
    REGEX MATCH
    "read_base_name\\(alt_driver->alt_path,[ \t\r\n]*entry->name,[ \t\r\n]*sizeof\\(entry->name\\)\\);"
    ALT_PATH_BASE_NAME_USAGE
    "${DMDEVFS_SOURCE}"
)

if(NOT ALT_PATH_BASE_NAME_USAGE)
    message(FATAL_ERROR "Alternative name listing is expected to use read_base_name(alt_driver->alt_path, ...)")
endif()
