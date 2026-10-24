#ifndef DMDEVFS_DRIVER_H
#define DMDEVFS_DRIVER_H

#include "dmdevfs_types.h"

/**
 * @brief creates new driver
 */
extern driver_t* driver_new( const char* module_name );

/**
 * @brief deletes the driver
 */
extern void driver_delete( driver_t* driver );

/**
 * @brief checks if the given driver structure is valid
 */
extern bool driver_is_valid( driver_t* driver );

/**
 * locks the driver
 */
extern bool driver_lock( driver_t* driver );

/**
 * @brief unlocks the given driver
 */
extern void driver_unlock( driver_t* driver );

#endif // DMDEVFS_DRIVER_H