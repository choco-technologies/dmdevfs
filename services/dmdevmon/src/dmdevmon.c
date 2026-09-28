#include "dmod.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "dmdrvi_ioctl.h"
#include "dmdevmon.h"
#include <errno.h>
#include <stdint.h>

/*
 * The monitor loop - the caller side of the dmdrvi monitor contract (see
 * dmdrvi's docs, "Monitor Ioctl Commands"). The node's driver never runs a
 * thread of its own; this loop does all the waiting for it.
 */

#define DMDEVMON_MAGIC                  0x444D4F4Eu     /* 'DMON' */

/** Pending events the semaphore counts before further posts are dropped. */
#define DMDEVMON_MAX_PENDING_EVENTS     16u

/** Upper bound of settle windows waited for events that keep coming (bounce). */
#define DMDEVMON_SETTLE_ROUNDS          10

struct dmdevmon
{
    uint32_t                magic;
    dmdevmon_ops_t          ops;
    dmdrvi_monitor_policy_t policy;
    dmosi_semaphore_t       wakeup;     /**< Posted by the event handler (ISR) and on a stop request */
    bool                    registered; /**< Event handler registered with dmhaman */
};

static bool is_valid(const dmdevmon_t* monitor)
{
    return monitor != NULL && monitor->magic == DMDEVMON_MAGIC;
}

/* dmhaman handler - interrupt context: never touches the node. */
static int on_event(void* parameters, void* user_ctx)
{
    (void)parameters;
    dmdevmon_t* monitor = (dmdevmon_t*)user_ctx;
    return is_valid(monitor) ? dmosi_semaphore_post(monitor->wakeup, 1) : -EINVAL;
}

static bool stop_requested(const dmdevmon_t* monitor)
{
    return monitor->ops.stop_requested();
}

static int register_handler(dmdevmon_t* monitor)
{
    if (monitor->policy.event_handler[0] == '\0')
    {
        return 0;
    }
    int ret = dmhaman_register_handler(monitor->policy.event_handler, on_event, monitor);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmdevmon: %s: cannot register event handler '%s' (%d)\n",
                       monitor->ops.name, monitor->policy.event_handler, ret);
        return (ret < 0) ? ret : -ENOMEM;
    }
    monitor->registered = true;
    return 0;
}

static int prepare(dmdevmon_t* monitor)
{
    int ret = monitor->ops.ioctl(monitor->ops.node, DMDRVI_IOCTL_MONITOR_GET_POLICY, &monitor->policy);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmdevmon: %s is not a monitored node (%d)\n", monitor->ops.name, ret);
        return ret;
    }
    /* Never trust the terminator of a string that crossed an ioctl. */
    monitor->policy.event_handler[DMDRVI_MONITOR_HANDLER_NAME_MAX - 1u] = '\0';

    monitor->wakeup = dmosi_semaphore_create(0, DMDEVMON_MAX_PENDING_EVENTS);
    return (monitor->wakeup != NULL) ? register_handler(monitor) : -ENOMEM;
}

dmdevmon_t* dmdevmon_create(const dmdevmon_ops_t* ops, int* error)
{
    int ret = -EINVAL;
    dmdevmon_t* monitor = NULL;
    if (ops != NULL && ops->ioctl != NULL && ops->stop_requested != NULL)
    {
        monitor = Dmod_Malloc(sizeof(*monitor));
        ret = (monitor != NULL) ? 0 : -ENOMEM;
    }
    if (monitor != NULL)
    {
        monitor->magic      = DMDEVMON_MAGIC;
        monitor->ops        = *ops;
        monitor->wakeup     = NULL;
        monitor->registered = false;
        ret = prepare(monitor);
        if (ret != 0)
        {
            dmdevmon_destroy(monitor);
            monitor = NULL;
        }
    }
    if (error != NULL)
    {
        *error = ret;
    }
    return monitor;
}

void dmdevmon_destroy(dmdevmon_t* monitor)
{
    if (!is_valid(monitor))
    {
        return;
    }
    if (monitor->registered)
    {
        /* First: the handler points into this monitor. */
        dmhaman_unregister_handler(monitor->policy.event_handler, on_event);
        monitor->registered = false;
    }
    if (monitor->wakeup != NULL)
    {
        dmosi_semaphore_destroy(monitor->wakeup);
        monitor->wakeup = NULL;
    }
    monitor->magic = 0;
    Dmod_Free(monitor);
}

dmosi_semaphore_t dmdevmon_get_wakeup(dmdevmon_t* monitor)
{
    return is_valid(monitor) ? monitor->wakeup : NULL;
}

static void refresh(dmdevmon_t* monitor)
{
    int ret = monitor->ops.ioctl(monitor->ops.node, DMDRVI_IOCTL_MONITOR_REFRESH, NULL);
    if (ret != 0 && ret != -ENODEV)
    {
        DMOD_LOG_WARN("dmdevmon: %s: refresh failed (%d)\n", monitor->ops.name, ret);
    }
}

/* Consume every pending post. Returns true if there was at least one. */
static bool drain(dmdevmon_t* monitor)
{
    bool any = false;
    while (dmosi_semaphore_wait(monitor->wakeup, 1, 0) == 0)
    {
        any = true;
    }
    return any;
}

/*
 * After an event: EVENT right away (the driver may have to act before the
 * state settles, e.g. abort I/O on a pulled card), then wait until a whole
 * settle window passes without a new event.
 */
static void settle(dmdevmon_t* monitor)
{
    for (int round = 0; round < DMDEVMON_SETTLE_ROUNDS; round++)
    {
        monitor->ops.ioctl(monitor->ops.node, DMDRVI_IOCTL_MONITOR_EVENT, NULL);
        dmosi_thread_sleep(monitor->policy.settle_ms);
        if (stop_requested(monitor) || !drain(monitor))
        {
            return;
        }
    }
}

static int32_t wait_timeout(const dmdevmon_t* monitor)
{
    uint32_t poll = monitor->policy.poll_interval_ms;
    if (poll == 0)
    {
        return -1;
    }
    return (poll > (uint32_t)INT32_MAX) ? INT32_MAX : (int32_t)poll;
}

int dmdevmon_run(dmdevmon_t* monitor)
{
    if (!is_valid(monitor))
    {
        return -EINVAL;
    }
    refresh(monitor);
    if (!monitor->registered && monitor->policy.poll_interval_ms == 0)
    {
        DMOD_LOG_INFO("dmdevmon: %s: no event handler and no polling - nothing to monitor\n", monitor->ops.name);
        return 0;
    }

    int32_t timeout = wait_timeout(monitor);
    while (!stop_requested(monitor))
    {
        bool woken = (dmosi_semaphore_wait(monitor->wakeup, 1, timeout) == 0);
        if (stop_requested(monitor))
        {
            break;
        }
        if (woken)
        {
            settle(monitor);
            if (stop_requested(monitor))
            {
                break;
            }
        }
        refresh(monitor);
    }
    return 0;
}
