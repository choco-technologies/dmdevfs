#ifndef DMDEVMON_H
#define DMDEVMON_H

#include <stdbool.h>
#include "dmosi.h"

/**
 * @file dmdevmon.h
 * @brief Monitor loop of the dmdevmon service (see dmdevmon.c)
 *
 * Private to the service: main.c runs it on a device node reached through
 * the file API, the tests run it on a node of a dmdevfs mount they own. The
 * loop only ever talks to the node through dmdevmon_ops_t::ioctl, so it
 * does not care how the node is reached.
 */

/**
 * @brief Issue one ioctl on the monitored node
 *
 * @param node    dmdevmon_ops_t::node.
 * @param command DMDRVI_IOCTL_MONITOR_* command.
 * @param arg     Command argument.
 * @return The driver's result (0 or a negative errno value).
 */
typedef int (*dmdevmon_ioctl_t)(void* node, int command, void* arg);

/**
 * @brief Whether the monitor has been asked to stop (checked after every wakeup)
 */
typedef bool (*dmdevmon_stop_requested_t)(void);

/** How the monitor reaches its node and learns that it has to stop. */
typedef struct
{
    dmdevmon_ioctl_t            ioctl;          /**< Required */
    dmdevmon_stop_requested_t   stop_requested; /**< Required */
    void*                       node;           /**< Passed to ioctl */
    const char*                 name;           /**< For log messages */
} dmdevmon_ops_t;

typedef struct dmdevmon dmdevmon_t;

/**
 * @brief Read the node's policy and prepare the monitor
 *
 * Calls DMDRVI_IOCTL_MONITOR_GET_POLICY, creates the wakeup semaphore and
 * registers the policy's event handler with dmhaman.
 *
 * @param ops    Node access and stop query; copied, @p ops->node and
 *               @p ops->name must outlive the monitor.
 * @param error  Receives 0 or a negative errno value: the GET_POLICY result
 *               (e.g. -ENOTTY: not a monitored node, -ENODEV: node not
 *               found), or -ENOMEM (also for a failed handler registration).
 * @return The monitor, or NULL on failure.
 */
dmdevmon_t* dmdevmon_create(const dmdevmon_ops_t* ops, int* error);

/**
 * @brief Unregister the event handler and release the monitor
 *
 * Also safe from a process exit callback after the thread running
 * dmdevmon_run() was killed.
 */
void dmdevmon_destroy(dmdevmon_t* monitor);

/**
 * @brief Semaphore the monitor sleeps on
 *
 * Post it (once) after making dmdevmon_ops_t::stop_requested return true -
 * e.g. by registering it with libsystemd_set_stop_semaphore().
 */
dmosi_semaphore_t dmdevmon_get_wakeup(dmdevmon_t* monitor);

/**
 * @brief Run the monitor loop
 *
 * Initial REFRESH, then until a stop is requested: wait for an event or the
 * poll interval; after events call EVENT once per settle round until the
 * events stop for a whole settle_ms, then REFRESH; after a poll timeout
 * REFRESH. Returns right after the initial REFRESH when the policy has
 * neither an event handler nor a poll interval.
 *
 * @return 0, or -EINVAL for an invalid monitor.
 */
int dmdevmon_run(dmdevmon_t* monitor);

#endif // DMDEVMON_H
