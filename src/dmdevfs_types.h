#ifndef DMDEVFS_TYPES_H
#define DMDEVFS_TYPES_H

#include "dmfsi.h"
#include "dmlist.h"
#include "dmdrvi.h"

/**
 * @brief stores lifecycle of the node
 */
typedef enum 
{
    lifecycle_always_on,    //!< The device is always available
    lifecycle_one_shot,     //!< The device is not needed anymore after configuration
    lifecycle_default       = lifecycle_always_on,      //!< Default lifecycle of the node
} lifecycle_t;

/**
 * @brief magic for dmfsi_context
 */
#define DMDEVFS_MAGIC       0x44455646      // DEVF

/**
 * @brief stores driver order. Smaller number are configured first
 */
typedef int32_t     device_order_t;

/**
 * @brief type for storing File System context
 */
struct dmfsi_context
{
    uint32_t            magic;          // DMDEVFS_MAGIC
    void*               mutex;
    dmlist_context_t*   drivers;        // driver_t
    dmlist_context_t*   nodes;          // device_node_t
};

#define DMDEVFS_DRIVER_MAGIC        0x64727663      // drvc

/**
 * @brief type for storing driver  
 */
typedef struct driver
{
    uint32_t            magic;          //!< DMDEVFS_DRIVER_MAGIC
    char*               name;           //!< Name of the driver
    Dmod_Context_t*     context;
    void*               mutex;
    struct 
    {
        dmod_dmdrvi_create_t    create;
    } api;
} driver_t;

/**
 * @brief magic for driver_node_t
 */
#define DMDEVFS_DRIVER_NODE_MAGIC   0x4452564E      // DRVN

/**
 * @brief type for storing driver nodes
 */
typedef struct device_node
{
    uint32_t            magic;          // DMDEVFS_DRIVER_NODE_MAGIC
    device_order_t      order;
    driver_t*           driver;         // reference to the driver
    char*               config_path;    //!< Path to the configuration file
    char*               section_name;   //!< Name of the section in the configuration file
    void*               mutex;          //!< Mutex for locking the device node
    char*               friends_group;  //!< Name of the friends group
    lifecycle_t         lifecycle;      //!< Lifecycle of the node
    dmdrvi_dev_num_t    dev_num;        //!< Device Node number
    dmdrvi_context_t    context;        //!< Device Node Context
} device_node_t;

#endif // DMDEVFS_TYPES_H