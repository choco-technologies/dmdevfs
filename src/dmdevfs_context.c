#include "dmdevfs_context.h"
#include "dmdevfs_node.h"
#include "dmdevfs_driver.h"

/**
 * @brief comparator for the dmlist
 */
int equal_pointers(const void* data1, const void* data2)
{
    return data1 == data2 ? 0 : 1;
}

/**
 * @brief checks if the given driver name is as expected
 */
int equal_driver_name( const void* data1, const void* data2 )
{
    driver_t* driver = data1;
    const char* name = data2;
    if(driver == NULL || name == NULL)
    {
        return -1;
    }
    return strcmp(driver->name, name);
}

/**
 * @brief destructor of the driver
 */
void driver_destructor(void* driver, void* user_data)
{
    driver_delete(driver);
}

/**
 * @brief destructor of the node
 */
void node_destructor(void* node, void* user_data)
{
    node_delete(node);
}

/**
 * @brief allocates new context
 */
dmfsi_context_t context_new()
{
    dmfsi_context_t context = Dmod_Malloc( sizeof(struct dmfsi_context) );
    if(context == NULL)
    {
        DMOD_LOG_ERROR("Cannot allocate memory for the dmdevfs context!\n");
        return NULL;
    }

    context->magic = DMDEVFS_MAGIC;
    context->mutex = Dmod_Mutex_New(true);
    if(context->mutex == NULL)
    {
        DMOD_LOG_ERROR("Cannot create mutex\n");
        context_delete(context);
        return NULL;
    }
    context->drivers = dmlist_create();
    context->nodes = dmlist_create();
    if(context->drivers == NULL || context->nodes == NULL)
    {
        DMOD_LOG_ERROR("Cannot create dmlist\n");
        context_delete(context);
        return NULL;
    }

    return context;
}

/**
 * @brief deletes the context
 */
void context_delete( dmfsi_context_t context )
{
    if(!context_is_valid(context))
    {
        DMOD_LOG_ERROR("Cannot delete context - it is invalid\n");
        return;
    }

    context->magic = 0;
    if(context->mutex != NULL)
    {
        Dmod_Mutex_Delete(context->mutex);
        context->mutex = NULL;
    }
    if(context->drivers != NULL)
    {
        dmlist_foreach(context->drivers, driver_destructor, NULL);
        dmlist_destroy(context->drivers);
    }
    if(context->nodes != NULL)
    {
        dmlist_foreach(context->nodes, node_destructor, NULL);
        dmlist_destroy(context->nodes);
    }

    Dmod_Free(context);
}

/**
 * @brief returns true if the context is valid
 */
bool context_is_valid( dmfsi_context_t context )
{
    Dmod_EnterCritical();
    bool valid = context != NULL && context->magic == DMDEVFS_MAGIC;
    Dmod_ExitCritical();
    return valid;
}

/**
 * @brief locks the context
 */
bool context_lock( dmfsi_context_t context )
{
    return Dmod_Mutex_Lock(context->mutex);
}

/**
 * @brief unlocks the context
 */
void context_unlock( dmfsi_context_t context )
{
    Dmod_Mutex_Unlock(context);
}

/**
 * @brief adds the given device node to the list
 */
bool context_add_node( dmfsi_context_t context, device_node_t* node )
{
    if(context_lock(context))
    {
        dmlist_push_back(context->nodes, node);
        context_unlock(context);
        DMOD_LOG_VERBOSE("Node has been added '%s' (section: '%s')\n", node->config_path, node->section_name != NULL ? node->section_name : "<global>");
        return true;
    }
    return false;
}

/**
 * @brief removes the given device node
 */
bool context_remove_node( dmfsi_context_t context, device_node_t* node )
{
    if(context_lock(context))
    {
        dmlist_remove(context->nodes, node, equal_pointers);
        context_unlock(context);
        DMOD_LOG_VERBOSE("Node has been removed '%s' (section: '%s')\n", node->config_path, node->section_name != NULL ? node->section_name : "<global>");
        return true;
    }

    return false;
}


/**
 * @brief adds the driver to the list
 */
bool context_add_driver( dmfsi_context_t context, driver_t* driver )
{
    if(context_lock(context))
    {
        dmlist_push_back(context->drivers, driver);
        context_unlock(context);
        DMOD_LOG_VERBOSE("Driver has been added '%s'\n", driver->name);
        return true;
    }
    return false;
}

/**
 * @brief removes a given driver
 */
bool context_remove_driver( dmfsi_context_t context, driver_t* driver )
{
    if(context_lock(context))
    {
        dmlist_remove(context->drivers, driver, equal_pointers);
        context_unlock(context);
        DMOD_LOG_VERBOSE("Driver has been removed '%s'\n", driver->name);
        return true;
    }

    return false;
}

/**
 * @brief finds a given driver
 */
driver_t* context_find_driver( dmfsi_context_t context, const char* driver_name )
{
    driver_t* driver = NULL;
    if(context_lock(context))
    {
        driver = dmlist_find(context->drivers, driver_name, equal_driver_name);
        context_unlock(context);
    }
    return driver;
}