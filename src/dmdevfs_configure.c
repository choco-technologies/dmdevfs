#include "dmdevfs_configure.h"
#include "dmdevfs_context.h"

/**
 * @brief configures all the device nodes
 */
bool configure_all( dmfsi_context_t context )
{
    return context_sort_nodes(context) 
        && context_configure_nodes(context);
}