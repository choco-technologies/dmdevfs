#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "dmdevmon.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>

/*
 * dmdevmon's monitor loop (services/dmdevmon/src/dmdevmon.c) against the
 * monitored nodes of the test driver (mockdrv/), on a dmdevfs mount of
 * fixtures/monitor owned by this test.
 *
 * The service's main.c reaches its node through the file API (dmvfs on a
 * target); the host loader has no dmvfs, so here the loop gets a node that
 * goes straight to the mount's dmfsi functions instead - the loop itself is
 * the same code. The mock driver counts the MONITOR_EVENT/_REFRESH calls it
 * receives (DMDEVFS_MOCKDRV_IOCTL_GET_STATS).
 */

#ifndef DMDEVFS_TEST_FIXTURES_DIR
#define DMDEVFS_TEST_FIXTURES_DIR "fixtures"
#endif

#define EVENT_HANDLER   "dmdevmon_test_event"
#define NODE_EVENTS     "/dmdevfs_mockdrv0"
#define NODE_POLLING    "/dmdevfs_mockdrv1"
#define NODE_IDLE       "/dmdevfs_mockdrv2"
#define NODE_PLAIN      "/dmdevfs_mockdrv3"
#define WAIT_MS         2000

typedef struct
{
    dmod_dmfsi_init_t       init;
    dmod_dmfsi_deinit_t     deinit;
    dmod_dmfsi_mounted_t    mounted;
    dmod_dmfsi_fopen_t      fopen;
    dmod_dmfsi_fclose_t     fclose;
    dmod_dmfsi_ioctl_t      ioctl;
} devfs_t;

typedef struct
{
    dmdevmon_t*     monitor;
    dmosi_thread_t  thread;
    volatile bool   done;
    int             ret;
} runner_t;

static devfs_t          g_fs;
static bool             g_ready;
static dmfsi_context_t  g_mount;
static volatile bool    g_stop;

static bool load_devfs(void)
{
    if (Dmod_LoadModuleByName("dmdevfs") == NULL || !Dmod_EnableModule("dmdevfs", false, NULL))
    {
        return false;
    }
    Dmod_Context_t* module = Dmod_GetModuleContext("dmdevfs");
    g_fs.init    = Dmod_GetDifFunction(module, dmod_dmfsi_init_sig);
    g_fs.deinit  = Dmod_GetDifFunction(module, dmod_dmfsi_deinit_sig);
    g_fs.mounted = Dmod_GetDifFunction(module, dmod_dmfsi_mounted_sig);
    g_fs.fopen   = Dmod_GetDifFunction(module, dmod_dmfsi_fopen_sig);
    g_fs.fclose  = Dmod_GetDifFunction(module, dmod_dmfsi_fclose_sig);
    g_fs.ioctl   = Dmod_GetDifFunction(module, dmod_dmfsi_ioctl_sig);
    return g_fs.init && g_fs.deinit && g_fs.mounted && g_fs.fopen && g_fs.fclose && g_fs.ioctl;
}

void dmod_test_setup(void)
{
    g_ready = g_ready || load_devfs();
    g_stop = false;
    g_mount = g_ready ? g_fs.init(DMDEVFS_TEST_FIXTURES_DIR "/monitor") : NULL;
    if (g_mount != NULL)
    {
        g_fs.mounted(g_mount, "/dev");
    }
}

void dmod_test_teardown(void)
{
    if (g_mount != NULL)
    {
        g_fs.deinit(g_mount);
        g_mount = NULL;
    }
}

/* ---- the node and stop query handed to the monitor ---- */

static int node_ioctl(void* node, int command, void* arg)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, (const char*)node, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return -ENODEV;
    }
    int ret = g_fs.ioctl(g_mount, file, command, arg);
    g_fs.fclose(g_mount, file);
    return ret;
}

static bool stop_requested(void)
{
    return g_stop;
}

static dmdevmon_t* create(const char* node, int* error)
{
    dmdevmon_ops_t ops = { .ioctl = node_ioctl, .stop_requested = stop_requested,
                           .node = (void*)node, .name = node };
    return dmdevmon_create(&ops, error);
}

static dmdevfs_mockdrv_stats_t stats(const char* node)
{
    dmdevfs_mockdrv_stats_t s = { 0, 0 };
    node_ioctl((void*)node, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &s);
    return s;
}

static bool wait_refreshes(const char* node, uint32_t count)
{
    for (int waited = 0; waited < WAIT_MS; waited += 5)
    {
        if (stats(node).refreshes >= count)
        {
            return true;
        }
        dmosi_thread_sleep(5);
    }
    return false;
}

/* ---- running the loop on its own thread ---- */

static void runner_entry(void* arg)
{
    runner_t* runner = (runner_t*)arg;
    runner->ret = dmdevmon_run(runner->monitor);
    runner->done = true;
}

static bool start(runner_t* runner, const char* node)
{
    runner->monitor = create(node, NULL);
    runner->done = false;
    runner->ret = -1;
    runner->thread = (runner->monitor != NULL)
        ? dmosi_thread_create(runner_entry, runner, 1, 4096 + DMOSI_THREAD_STACK_OVERHEAD,
                              "dmdevmon_test", dmosi_process_current())
        : NULL;
    return runner->thread != NULL;
}

/** What libsystemd does: raise the stop flag, post the wakeup semaphore once. */
static void stop(runner_t* runner)
{
    g_stop = true;
    dmosi_semaphore_post(dmdevmon_get_wakeup(runner->monitor), 1);
    dmosi_thread_join(runner->thread);
    dmosi_thread_destroy(runner->thread);
    dmdevmon_destroy(runner->monitor);
    runner->monitor = NULL;
}

/* ---- steps ---- */

DMOD_TEST_STEP(dmdevmon_refreshes_once_when_there_is_nothing_to_wait_for)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    int error = -1;
    dmdevmon_t* monitor = create(NODE_IDLE, &error);
    DMOD_TEST_EXPECT_NOT_NULL(monitor);
    DMOD_TEST_EXPECT_EQ(error, 0);
    DMOD_TEST_EXPECT_EQ(dmdevmon_run(monitor), 0);     /* returns on its own */
    DMOD_TEST_EXPECT_EQ(stats(NODE_IDLE).refreshes, 1u);
    DMOD_TEST_EXPECT_EQ(stats(NODE_IDLE).events, 0u);
    dmdevmon_destroy(monitor);
}

DMOD_TEST_STEP(dmdevmon_rejects_node_that_is_not_monitored)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    int error = 0;
    DMOD_TEST_EXPECT_NULL(create(NODE_PLAIN, &error));
    DMOD_TEST_EXPECT_EQ(error, -ENOTTY);
    DMOD_TEST_EXPECT_NULL(create("/does_not_exist", &error));
    DMOD_TEST_EXPECT_EQ(error, -ENODEV);
}

DMOD_TEST_STEP(dmdevmon_polls_at_the_policy_interval)
{
    runner_t runner;
    DMOD_TEST_EXPECT_TRUE(start(&runner, NODE_POLLING));
    dmosi_thread_sleep(290);                            /* 50 ms interval */
    stop(&runner);
    DMOD_TEST_EXPECT_TRUE(runner.done);
    DMOD_TEST_EXPECT_EQ(runner.ret, 0);
    dmdevfs_mockdrv_stats_t s = stats(NODE_POLLING);
    DMOD_TEST_EXPECT_TRUE(s.refreshes >= 1u + 3u);     /* initial + polls */
    DMOD_TEST_EXPECT_TRUE(s.refreshes <= 1u + 7u);
    DMOD_TEST_EXPECT_EQ(s.events, 0u);
}

DMOD_TEST_STEP(dmdevmon_settles_an_event_burst_into_one_refresh)
{
    runner_t runner;
    DMOD_TEST_EXPECT_TRUE(start(&runner, NODE_EVENTS));
    DMOD_TEST_EXPECT_TRUE(wait_refreshes(NODE_EVENTS, 1));     /* initial */

    for (int i = 0; i < 5; i++)                                /* bounce: 5 ms apart, 30 ms settle */
    {
        DMOD_TEST_EXPECT_EQ(dmhaman_call_handler(EVENT_HANDLER, NULL), 0);
        dmosi_thread_sleep(5);
    }
    DMOD_TEST_EXPECT_TRUE(wait_refreshes(NODE_EVENTS, 2));
    dmosi_thread_sleep(150);                                   /* nothing else follows */
    dmdevfs_mockdrv_stats_t s = stats(NODE_EVENTS);
    stop(&runner);

    DMOD_TEST_EXPECT_EQ(s.refreshes, 2u);
    DMOD_TEST_EXPECT_TRUE(s.events >= 1u);                     /* EVENT before settling */
    DMOD_TEST_EXPECT_TRUE(s.events <= 5u);
}

DMOD_TEST_STEP(dmdevmon_registers_event_handler_only_while_alive)
{
    runner_t runner;
    DMOD_TEST_EXPECT_NULL(dmhaman_get_handler(EVENT_HANDLER));
    DMOD_TEST_EXPECT_TRUE(start(&runner, NODE_EVENTS));
    DMOD_TEST_EXPECT_NOT_NULL(dmhaman_get_handler(EVENT_HANDLER));

    stop(&runner);
    DMOD_TEST_EXPECT_NULL(dmhaman_get_handler(EVENT_HANDLER));
}

DMOD_TEST_STEP(dmdevmon_stops_promptly_when_asked)
{
    runner_t runner;
    DMOD_TEST_EXPECT_TRUE(start(&runner, NODE_EVENTS));        /* no polling: sleeps until woken */
    DMOD_TEST_EXPECT_TRUE(wait_refreshes(NODE_EVENTS, 1));

    uint64_t begin = (uint64_t)Dmod_GetUptime();
    stop(&runner);
    uint64_t elapsed = (uint64_t)Dmod_GetUptime() - begin;

    DMOD_TEST_EXPECT_TRUE(runner.done);
    DMOD_TEST_EXPECT_TRUE(elapsed < 200u);
    DMOD_TEST_EXPECT_EQ(stats(NODE_EVENTS).refreshes, 1u);     /* a stop is no refresh */
}
