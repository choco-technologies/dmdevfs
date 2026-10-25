/**
 * @file mountcfg.c
 * @brief Loads the configuration directory of every mount point
 *
 * A mounted file system - an SD card, the external flash - may bring its own
 * configuration, in the layout of the firmware's /cfg:
 *
 *     <mount point>/cfg/drivers    driver configurations  -> dmdevfs
 *     <mount point>/cfg/services   units                  -> libsystemd
 *     <mount point>/cfg/rules      device rules           -> libsystemd
 *
 * mountcfg implements dmvfs's _mount_changed DIF (dmvfs_dif.h) - dmvfs calls it
 * for every mount and unmount - and hands the directory to both, in order: the
 * drivers before the services that may use them, and the other way round.
 *
 * The work is done on mountcfg's own thread, not in the DIF call: dmvfs calls
 * it in whatever thread mounts (automount's, with a small stack), and loading
 * a directory starts units - loads their modules from the card.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmosi.h"
#include "dmlist.h"
#include "dmvfs.h"          // dmvfs_get_mount_point()
#include "dmvfs_dif.h"      // _mount_changed
#include "libsystemd.h"
#include "dmdevfs_cfg.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

/**
 * @brief The worker thread: starting a unit loads its module (from a card,
 *        decompressing a .dmfc) on this stack
 */
#define MOUNTCFG_THREAD_NAME        "mountcfg"
#define MOUNTCFG_THREAD_PRIORITY    1
#define MOUNTCFG_THREAD_STACK_SIZE  (4096 + DMOSI_THREAD_STACK_OVERHEAD)

/**
 * @brief How long an unmount waits for the units of its file system to stop
 *
 * Every unit may take its stop_timeout_ms (3 s by default) - beyond this the
 * file system is unmounted anyway.
 */
#define MOUNTCFG_UNMOUNT_WAIT_MS    30000

/**
 * @brief Longest mount point read with dmvfs_get_mount_point()
 */
#define MOUNTCFG_MAX_PATH           128

/**
 * @brief One mount or unmount for the worker thread
 *
 * mount_point == NULL: the shutdown sentinel (see dmod_deinit()).
 */
typedef struct
{
    char* mount_point;              // Owned copy
    bool mounted;
    dmosi_semaphore_t done;         // Not NULL: posted once handled (an unmount waits for it)
} mountcfg_event_t;

static dmlist_context_t*  g_events    = NULL;
static dmosi_mutex_t      g_mutex     = NULL;
static dmosi_semaphore_t  g_pending   = NULL;
static dmosi_process_t    g_process   = NULL;
static dmosi_thread_t     g_thread    = NULL;
static volatile bool      g_running   = false;   // The worker takes events (between dmod_init() and dmod_deinit())

/**
 * @brief Load or unload the configuration directory of one mount point
 *
 * Mounted: the drivers first (dmdevfs configures them on its own thread), then
 * the services and rules - a service started by a device rule waits for its
 * device anyway. About to be unmounted: the services first, while their files
 * are still there, then the drivers.
 */
static void handle(const char* mount_point, bool mounted)
{
    // Connected unless an older dmdevfs/libsystemd without these is installed
    bool devfs = Dmod_IsFunctionConnected((void*)dmdevfs_add_config_dir) &&
                 Dmod_IsFunctionConnected((void*)dmdevfs_remove_config_dir);
    bool systemd = Dmod_IsFunctionConnected((void*)libsystemd_add_config_dir) &&
                   Dmod_IsFunctionConnected((void*)libsystemd_remove_config_dir);

    if (mounted)
    {
        if (devfs)
        {
            dmdevfs_add_config_dir(mount_point);
        }
        int ret = systemd ? libsystemd_add_config_dir(mount_point) : -ENOSYS;
        if (ret != 0 && ret != -ENOENT && ret != -EALREADY)
        {
            DMOD_LOG_WARN("mountcfg: cannot load the services of %s (%d)\n", mount_point, ret);
        }
    }
    else
    {
        if (systemd)
        {
            libsystemd_remove_config_dir(mount_point);
        }
        if (devfs)
        {
            dmdevfs_remove_config_dir(mount_point);
        }
    }
}

static void worker_thread(void* arg)
{
    (void)arg;

    // The mounts made before mountcfg started - on this stack, not on the one of
    // whoever enabled the module. A mount also reported meanwhile is loaded once
    // (a repeated report is a no-op).
    char mount_point[MOUNTCFG_MAX_PATH];
    for (int i = 0; dmvfs_get_mount_point(i, mount_point, sizeof(mount_point)) != -ENOENT; i++)
    {
        handle(mount_point, true);
    }

    for (;;)
    {
        if (dmosi_semaphore_wait(g_pending, 1, -1) != 0)
        {
            continue;
        }

        dmosi_mutex_lock(g_mutex);
        mountcfg_event_t* event = (mountcfg_event_t*)dmlist_pop_front(g_events);
        dmosi_mutex_unlock(g_mutex);
        if (event == NULL)
        {
            continue;
        }
        if (event->mount_point == NULL)
        {
            Dmod_Free(event);
            break;
        }

        handle(event->mount_point, event->mounted);

        Dmod_Free(event->mount_point);
        if (event->done != NULL)
        {
            dmosi_semaphore_post(event->done, 1);   // The waiter frees the event
        }
        else
        {
            Dmod_Free(event);
        }
    }
}

/**
 * @brief Queue an event for the worker thread
 *
 * @return true if queued (the worker owns it from now on, unless it has a done semaphore)
 */
static bool queue_event(mountcfg_event_t* event)
{
    dmosi_mutex_lock(g_mutex);
    bool queued = dmlist_push_back(g_events, event);
    dmosi_mutex_unlock(g_mutex);
    if (queued)
    {
        dmosi_semaphore_post(g_pending, 1);
    }
    return queued;
}

/**
 * @brief Hand a mount or an unmount to the worker
 *
 * A mount is not waited for. An unmount waits until the worker has stopped the
 * units of the file system - they still need their files - but not longer than
 * MOUNTCFG_UNMOUNT_WAIT_MS.
 */
static void report(const char* mount_point, bool mounted)
{
    if (!g_running)
    {
        return;
    }

    mountcfg_event_t* event = Dmod_Malloc(sizeof(mountcfg_event_t));
    if (event == NULL)
    {
        DMOD_LOG_ERROR("mountcfg: out of memory - %s is not %s\n", mount_point, mounted ? "loaded" : "unloaded");
        return;
    }
    memset(event, 0, sizeof(*event));
    event->mount_point = Dmod_StrDup(mount_point);
    event->mounted = mounted;
    event->done = mounted ? NULL : dmosi_semaphore_create(0, 1);
    if (event->mount_point == NULL || (!mounted && event->done == NULL) || !queue_event(event))
    {
        DMOD_LOG_ERROR("mountcfg: out of memory - %s is not %s\n", mount_point, mounted ? "loaded" : "unloaded");
        if (event->done != NULL)
        {
            dmosi_semaphore_destroy(event->done);
        }
        Dmod_Free(event->mount_point);
        Dmod_Free(event);
        return;
    }

    if (!mounted)
    {
        if (dmosi_semaphore_wait(event->done, 1, MOUNTCFG_UNMOUNT_WAIT_MS) != 0)
        {
            // Still running: the worker posts later and nobody frees the event
            DMOD_LOG_WARN("mountcfg: the services of %s did not stop in time\n", mount_point);
            return;
        }
        dmosi_semaphore_destroy(event->done);
        Dmod_Free(event);
    }
}

/**
 * @brief dmvfs's _mount_changed DIF: a file system was mounted or is about to be unmounted
 */
dmod_dmvfs_dif_api_declaration( 1.0, mountcfg, void, _mount_changed, (const char* mount_point, bool mounted) )
{
    report(mount_point, mounted);
}

/**
 * @brief Module initialization: start the worker
 *
 * dmvfs reports the mounts made from now on (the DIF above); the worker loads
 * the ones made before (dmvfs_get_mount_point()) first.
 */
int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;

    g_events  = dmlist_create();
    g_mutex   = dmosi_mutex_create(false);
    g_pending = dmosi_semaphore_create(0, UINT32_MAX);
    if (g_events == NULL || g_mutex == NULL || g_pending == NULL)
    {
        DMOD_LOG_ERROR("mountcfg: out of memory\n");
        return -ENOMEM;
    }

    g_process = dmosi_process_create(MOUNTCFG_THREAD_NAME, DMOD_MODULE_NAME, NULL);
    g_thread = (g_process != NULL)
             ? dmosi_thread_create(worker_thread, NULL, MOUNTCFG_THREAD_PRIORITY, MOUNTCFG_THREAD_STACK_SIZE,
                                   MOUNTCFG_THREAD_NAME, g_process)
             : NULL;
    if (g_thread == NULL)
    {
        DMOD_LOG_ERROR("mountcfg: cannot start the worker thread\n");
        return -ENOMEM;
    }

    g_running = true;
    return 0;
}

/**
 * @brief Module deinitialization: stop the worker
 *
 * What was loaded stays loaded - it goes when the system goes down.
 */
int dmod_deinit(void)
{
    g_running = false;

    if (g_thread != NULL)
    {
        mountcfg_event_t* stop = Dmod_Malloc(sizeof(mountcfg_event_t));
        if (stop != NULL)
        {
            memset(stop, 0, sizeof(*stop));
            if (!queue_event(stop))
            {
                Dmod_Free(stop);
            }
        }
        dmosi_thread_join(g_thread);
        dmosi_thread_destroy(g_thread);
        g_thread = NULL;
    }
    if (g_process != NULL)
    {
        dmosi_process_destroy(g_process);
        g_process = NULL;
    }

    mountcfg_event_t* event;
    while (g_events != NULL && (event = (mountcfg_event_t*)dmlist_pop_front(g_events)) != NULL)
    {
        Dmod_Free(event->mount_point);
        Dmod_Free(event);
    }
    dmlist_destroy(g_events);
    g_events = NULL;
    dmosi_semaphore_destroy(g_pending);
    g_pending = NULL;
    dmosi_mutex_destroy(g_mutex);
    g_mutex = NULL;
    return 0;
}
