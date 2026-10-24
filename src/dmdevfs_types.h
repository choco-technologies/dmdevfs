#ifndef DMDEVFS_TYPES_H
#define DMDEVFS_TYPES_H

#include "dmfsi.h"
#include "dmlist.h"

/**
 * @brief magic for dmfsi_context
 */
#define DMDEVFS_MAGIC       0x44455646      // DEVF

/**
 * @brief stores driver order. Smaller number are configured first
 */
typedef int32_t     driver_order_t;

/**
 * @brief type for storing File System context
 */
struct dmfsi_context
{
    uint32_t            magic;          // DMDEVFS_MAGIC
    void*               mutex;
    dmlist_context_t*   drivers;        // driver_t
};

#define DMDEVFS_DRIVER_MAGIC        0x64727663      // drvc

/**
 * @brief type for storing driver  
 */
typedef struct driver
{
    uint32_t            magic;          // DMDEVFS_DRIVER_MAGIC
    Dmod_Context_t      context;
    void*               mutex;
} driver_t;

/**
 * @brief magic for driver_node_t
 */
#define DMDEVFS_DRIVER_NODE_MAGIC   0x4452564E      // DRVN

/**
 * @brief type for storing driver nodes
 */
typedef struct driver_node
{
    uint32_t            magic;          // DMDEVFS_DRIVER_NODE_MAGIC
    driver_order_t      order;
} driver_node_t;

#endif // DMDEVFS_TYPES_H