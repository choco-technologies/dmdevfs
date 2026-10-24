#ifndef DMDEVFS_SCAN_H
#define DMDEVFS_SCAN_H

#include "dmdevfs_types.h"

/**
 * @brief scans the given configuration directory
 */
extern bool scan( dmfsi_context_t context, const char* path, const char* driver_name );

/**
 * @brief scans the given configuration file
 */
extern bool scan_file( dmfsi_context_t context, const char* path, const char* driver_name );

#endif // DMDEVFS_SCAN_H