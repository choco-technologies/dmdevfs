#ifndef DMDEVFS_DMDRVI_H
#define DMDEVFS_DMDRVI_H

#include "dmdevfs_types.h"
#include "dmdrvi.h"

/**
 * @brief calls the create function of the dmdrvi interface
 */
extern dmdrvi_context_t dmdrvi_create ( driver_t* driver, dmini_context_t config, dmdrvi_dev_num_t* dev_num );

#endif // DMDEVFS_DMDRVI_H