#ifndef DMDEVFS_CONTEXT_H
#define DMDEVFS_CONTEXT_H

#include "dmdevfs_types.h"

/**
 * @brief allocates new context
 */
extern dmfsi_context_t context_new();

/**
 * @brief deletes the context
 */
extern void context_delete( dmfsi_context_t context );

/**
 * @brief returns true if the context is valid
 */
extern bool context_is_valid( dmfsi_context_t context );

/**
 * @brief locks the context
 */
extern bool context_lock( dmfsi_context_t context );

/**
 * @brief unlocks the context
 */
extern void context_unlock( dmfsi_context_t context );

/**
 * @brief adds the given device node to the list
 */
extern bool context_add_node( dmfsi_context_t context, device_node_t* node );

/**
 * @brief removes the given device node
 */
extern bool context_remove_node( dmfsi_context_t context, device_node_t* node );

/**
 * @brief adds the driver to the list
 */
extern bool context_add_driver( dmfsi_context_t context, driver_t* driver );

/**
 * @brief removes a given drover
 */
extern bool context_remove_driver( dmfsi_context_t context, driver_t* driver );

/**
 * @brief finds a given driver
 */
extern driver_t* context_find_driver( dmfsi_context_t context, const char* driver_name );

#endif // DMDEVFS_CONTEXT_H