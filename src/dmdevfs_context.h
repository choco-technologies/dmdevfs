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

#endif // DMDEVFS_CONTEXT_H