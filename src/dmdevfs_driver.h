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

/**
 * @brief marks the driver as used
 */
extern void driver_begin_usage( driver_t* driver );

/**
 * @brief marks the driver as not used
 */
extern void driver_end_usage( driver_t* driver );

/**
 * @brief checks if the driver is loaded
 */
extern bool driver_is_loaded( driver_t* driver );

/**
 * @brief checks if the driver is enabled
 */
extern bool driver_is_enabled( driver_t* driver );

/**
 * @brief loads the driver
 */
extern bool driver_load( driver_t* driver );

/**
 * @brief enables the driver
 */
extern bool driver_enable( driver_t* driver );

/**
 * @brief clears the cache of the driver
 */
extern void driver_clear_cache( driver_t* driver );

#endif // DMDEVFS_DRIVER_H