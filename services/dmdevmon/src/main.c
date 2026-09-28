#include "dmod.h"
#include "dmosi.h"
#include "libsystemd.h"
#include "dmdevmon.h"
#include <errno.h>

/*
 * dmdevmon <node> - monitor service for one device node.
 *
 * dmdevfs reports every node whose driver answers
 * DMDRVI_IOCTL_MONITOR_GET_POLICY to libsystemd as class "monitor"; the
 * [class=monitor] rule in configs/dmdevmon.rules starts dmdevmon@<name>
 * from configs/dmdevmon@.ini with the node's absolute path as argument.
 * The loop itself is in dmdevmon.c.
 */

/* One open/ioctl/close per request: nothing stays open if the unit is killed. */
static int node_ioctl(void* node, int command, void* arg)
{
    void* file = Dmod_FileOpen((const char*)node, "r");
    if (file == NULL)
    {
        return -ENODEV;
    }
    int ret = Dmod_Ioctl(file, command, arg);
    Dmod_FileClose(file);
    return ret;
}

static bool stop_requested(void)
{
    return libsystemd_stop_requested();
}

/* Only reached when the unit had to be killed: main() never returned. */
static void on_killed(dmosi_process_t process, int exit_status, void* arg)
{
    (void)process;
    (void)exit_status;
    dmdevmon_destroy((dmdevmon_t*)arg);
}

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage: %s <node>\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Monitor service for one device node (e.g. /dev/dmsdio0): waits for the\n");
    Dmod_Printf("events and poll interval the node's driver asks for and calls its\n");
    Dmod_Printf("DMDRVI_IOCTL_MONITOR_EVENT/_REFRESH. Normally started by libsystemd from\n");
    Dmod_Printf("dmdevmon@.ini for every node dmdevfs reports as \"monitor\".\n");
}

/*
 * Only resource shortage is worth a restart (restart=on-failure); a node that
 * is not monitored (or gone) stays that way - restarting would only spin.
 */
static int exit_status(int error)
{
    return (error == -ENOMEM) ? error : 0;
}

static int run(dmdevmon_t* monitor)
{
    dmosi_process_t self = dmosi_process_current();
    dmosi_process_exit_callback_handle_t killed =
        (self != NULL) ? dmosi_process_register_exit_callback(self, on_killed, monitor) : NULL;
    if (killed == NULL)
    {
        DMOD_LOG_ERROR("dmdevmon: cannot register exit callback\n");
        return -ENOMEM;
    }

    libsystemd_set_stop_semaphore(dmdevmon_get_wakeup(monitor));
    int ret = dmdevmon_run(monitor);
    libsystemd_set_stop_semaphore(NULL);

    dmosi_process_unregister_exit_callback(self, killed);
    return ret;
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1][0] == '\0')
    {
        print_usage(argv[0]);
        return -EINVAL;
    }

    dmdevmon_ops_t ops = {
        .ioctl          = node_ioctl,
        .stop_requested = stop_requested,
        .node           = argv[1],
        .name           = argv[1],
    };
    int error = 0;
    dmdevmon_t* monitor = dmdevmon_create(&ops, &error);
    if (monitor == NULL)
    {
        return exit_status(error);
    }

    int ret = run(monitor);
    dmdevmon_destroy(monitor);
    return ret;
}
