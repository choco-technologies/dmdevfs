#ifndef DMDEVFS_SCAN_H
#define DMDEVFS_SCAN_H

#include "dmdevfs_types.h"

/**
 * @brief scans the given configuration directory
 */
extern bool scan( dmfsi_context_t context, const char* path );

#endif // DMDEVFS_SCAN_H