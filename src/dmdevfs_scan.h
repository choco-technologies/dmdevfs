#ifndef DMDEVFS_SCAN_H
#define DMDEVFS_SCAN_H

#include "dmdevfs_types.h"
#include "dmini.h"
#include "dmfsi.h"

/**
 * @brief scans the given configuration directory
 */
extern bool scan( dmfsi_context_t context, const char* path, const char* dir_name );

/**
 * @brief scans the given configuration file
 */
extern bool scan_file( dmfsi_context_t context, const char* path, const char* dir_name );

/**
 * @brief scans the given section in the configuration file
 */
extern bool scan_section( dmfsi_context_t context, dmini_context_t ini_context, const char* section_name, const char* cfg_path, const char* dir_name );

#endif // DMDEVFS_SCAN_H