#include "dmdevfs_context.h"
#include "dmdevfs_node.h"

/**
 * @brief comparator for the dmlist
 */
int equal_pointers(const void* data1, const void* data2)
{
    return data1 == data2;
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
device_node_t* context_add_node( dmfsi_context_t context, device_node_t* node )
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