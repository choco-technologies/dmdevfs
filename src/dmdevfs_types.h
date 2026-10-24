#ifndef DMDEVFS_TYPES_H
#define DMDEVFS_TYPES_H

#include "dmfsi.h"
#include "dmlist.h"

#define DMDEVFS_MAGIC       0x44455646      // DEVF

/**
 * @brief type for storing File System context
 */
struct dmfsi_context
{
    uint32_t            magic;
    void*               mutex;
    // dmlist_context_t*   nodes;
};

/**
 * @brief type for storing driver nodes
 */
typedef struct driver_node
{

} driver_node_t;

#endif // DMDEVFS_TYPES_H