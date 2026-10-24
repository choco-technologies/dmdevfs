#include "dmdevfs_node.h"


/**
 * @brief creates the device node
 */
device_node_t* node_new( const char* cfg_path, const char* section_name, driver_t* driver, device_order_t device_order )
{
    device_node_t* node = Dmod_Malloc(sizeof(device_node_t));
    if(node == NULL)
    {
        DMOD_LOG_ERROR("Cannot allocate memory for device node '%s' (section: '%s')\n", cfg_path, section_name != NULL ? section_name : "<global>");
        return NULL;
    }

    node->magic = DMDEVFS_DRIVER_NODE_MAGIC;
    node->driver = driver;
    node->order = device_order;
    node->config_path = Dmod_StrDup(cfg_path);
    node->section_name = NULL;
    node->mutex = Dmod_Mutex_New(true);
    if(section_name != NULL)
    {
        node->section_name = Dmod_StrDup(section_name);
    }

    if(node->mutex == NULL || node->config_path == NULL || (section_name != NULL && node->section_name == NULL))
    {
        DMOD_LOG_ERROR("Cannot allocate memory for node '%s' (section: '%s')\n", cfg_path, section_name != NULL ? section_name : "<global>");
        node_delete(node);
        return NULL;
    }

    return node;
}

/**
 * @brief deletes the device node
 */
void node_delete( device_node_t* node )
{
    if(!node_is_valid(node))
    {
        DMOD_LOG_ERROR("Cannot delete node - it is invalid\n");
        return false;
    }

    node->magic = 0;
    if(node->config_path != NULL)
    {
        Dmod_Free(node->config_path);
    }
    if(node->section_name != NULL)
    {
        Dmod_Free(node->section_name);
    }
    if(node->mutex != NULL)
    {
        Dmod_Mutex_Delete(node->mutex);
    }

    Dmod_Free(node);
}

/**
 * @brief checks if the given node is valid
 */
bool node_is_valid( device_node_t* node )
{
    Dmod_EnterCritical();
    bool valid = node != NULL && node->magic == DMDEVFS_DRIVER_NODE_MAGIC;
    Dmod_ExitCritical();
    return valid;
}

/**
 * @brief locks the node
 */
extern bool node_lock( device_node_t* node )
{
    return node_is_valid(node) && Dmod_Mutex_Lock(node);
}

/**
 * @brief unlocks the node
 */
void node_unlock( device_node_t* node)
{
    if(node_is_valid(node))
    {
        Dmod_Mutex_Unlock(node);
    }
}