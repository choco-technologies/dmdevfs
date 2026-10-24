#include "dmdevfs_context.h"

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