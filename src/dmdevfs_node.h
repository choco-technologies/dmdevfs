#ifndef DMDEVFS_NODE_H
#define DMDEVFS_NODE_H

#include "dmdevfs_types.h"

/**
 * @brief creates the device node
 */
extern device_node_t* node_new( const char* cfg_path, const char* section_name, driver_t* driver, device_order_t order );

/**
 * @brief deletes the device node
 */
extern void node_delete( device_node_t* node );

/**
 * @brief checks if the given node is valid
 */
extern bool node_is_valid( device_node_t* node );

/**
 * @brief locks the node
 */
extern bool node_lock( device_node_t* node );

/**
 * @brief unlocks the node
 */
extern void node_unlock( device_node_t* node);

#endif // DMDEVFS_NODE_H