#ifndef DMDEVFS_NODE_H
#define DMDEVFS_NODE_H

#include "dmdevfs_types.h"

/**
 * @brief creates the device node
 */
extern device_node_t* node_new( const char* cfg_path, const char* section_name );

/**
 * @brief deletes the device node
 */
extern void node_delete( device_node_t* node );

#endif // DMDEVFS_NODE_H