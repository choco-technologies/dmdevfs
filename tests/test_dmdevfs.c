#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "libsystemd.h"
#include "dmdevfs_mockdrv.h"
#include "dmdrvi_ioctl.h"
#include <string.h>
#include <errno.h>

/*
 * libsystemd node reporting.
 *
 * dmdevfs is mounted at /dev from fixtures/config with the test driver
 * dmdevfs_mockdrv (mockdrv/). libsystemd has the units from fixtures/units
 * and the rules from fixtures/rules loaded, so every report dmdevfs makes
 * starts a real process (dmdevfs_testsvc) as mon@<name> ("monitor"),
 * blk@<name> ("block"), dsp@<name> ("display") or inp@<name> ("input"), and
 * every removal stops it again - both observable
 * through libsystemd_status(). A unit that was never reported does not exist
 * at all (-ENOENT).
 *
 * A unit is only ever stopped once its service is fully up (wait_ready()):
 * killing a process still starting up inside the loader can leave a global
 * loader lock held and hang every later module operation.
 */

#ifndef DMDEVFS_TEST_FIXTURES_DIR
#define DMDEVFS_TEST_FIXTURES_DIR "fixtures"
#endif

#define WAIT_MS     3000

typedef struct
{
    dmod_dmfsi_init_t       init;
    dmod_dmfsi_deinit_t     deinit;
    dmod_dmfsi_mounted_t    mounted;
    dmod_dmfsi_fopen_t      fopen;
    dmod_dmfsi_fclose_t     fclose;
    dmod_dmfsi_ioctl_t      ioctl;
} devfs_t;

static devfs_t          g_fs;
static bool             g_ready;
static dmfsi_context_t  g_mount;

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

static bool wait_ready(const char* unit, const char* path);

void dmod_test_setup(void)
{
    if (!g_ready)
    {
        g_ready = load_devfs() &&
                  libsystemd_scan(DMDEVFS_TEST_FIXTURES_DIR "/units") == 0 &&
                  libsystemd_load_rules(DMDEVFS_TEST_FIXTURES_DIR "/rules") == 0;
    }
    g_mount = g_ready ? g_fs.init(DMDEVFS_TEST_FIXTURES_DIR "/config") : NULL;
    if (g_mount != NULL)
    {
        g_fs.mounted(g_mount, "/dev");
        /* Every mount starts mon@dmdevfs_mockdrv0, dsp@dmdevfs_mockdrv3 and
         * inp@dmdevfs_mockdrv4 - let them come up before any step (or the
         * teardown) may stop them. */
        wait_ready("mon@dmdevfs_mockdrv0", "/dev/dmdevfs_mockdrv0");
        wait_ready("dsp@dmdevfs_mockdrv3", "/dev/dmdevfs_mockdrv3");
        wait_ready("inp@dmdevfs_mockdrv4", "/dev/dmdevfs_mockdrv4");
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

/** State of @p unit, or DMOSI_PROCESS_STATE_CREATED - 1 when it does not exist. */
static int unit_state(const char* unit)
{
    libsystemd_service_status_t status;
    return (libsystemd_status(unit, &status) == 0) ? (int)status.state : -1;
}

static bool wait_state(const char* unit, dmosi_process_state_t state)
{
    for (int waited = 0; waited < WAIT_MS; waited += 10)
    {
        if (unit_state(unit) == (int)state)
        {
            return true;
        }
        dmosi_thread_sleep(10);
    }
    return false;
}

/**
 * Wait until @p unit is running and its service is set up (it registers a
 * dmhaman handler named after its node path, see testsvc/testsvc.c).
 */
static bool wait_ready(const char* unit, const char* path)
{
    if (!wait_state(unit, DMOSI_PROCESS_STATE_RUNNING))
    {
        return false;
    }
    for (int waited = 0; waited < WAIT_MS; waited += 10)
    {
        if (dmhaman_get_handler(path) != NULL)
        {
            return true;
        }
        dmosi_thread_sleep(10);
    }
    return false;
}

/** Plug or unplug the block child of /dev/dmdevfs_mockdrv<major> through dmdevfs. */
static int set_plugged(const char* host, bool plugged)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, host, DMFSI_O_RDWR, 0) != DMFSI_OK)
    {
        return -ENOENT;
    }
    int ret = g_fs.ioctl(g_mount, file, plugged ? DMDEVFS_MOCKDRV_IOCTL_PLUG : DMDEVFS_MOCKDRV_IOCTL_UNPLUG, NULL);
    g_fs.fclose(g_mount, file);
    return ret;
}

/** Driver statistics of /dev/dmdevfs_mockdrv<major>, fetched through dmdevfs (one host open). */
static int get_stats(const char* host, dmdevfs_mockdrv_stats_t* stats)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, host, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return -ENOENT;
    }
    int ret = g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, stats);
    g_fs.fclose(g_mount, file);
    return ret;
}

DMOD_TEST_STEP(dmdevfs_reports_monitored_node_and_its_removal)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(wait_ready("mon@dmdevfs_mockdrv0", "/dev/dmdevfs_mockdrv0"));

    g_fs.deinit(g_mount);
    g_mount = NULL;
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_TERMINATED));
}

DMOD_TEST_STEP(dmdevfs_skips_opted_out_and_unmonitored_nodes)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(wait_ready("mon@dmdevfs_mockdrv0", "/dev/dmdevfs_mockdrv0"));
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv1"), -1);    /* report=none */
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv2"), -1);    /* no GET_POLICY */
    DMOD_TEST_EXPECT_EQ(unit_state("blk@dmdevfs_mockdrv0"), -1);    /* host node is no block device */
}

DMOD_TEST_STEP(dmdevfs_reports_display_and_input_nodes)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(wait_ready("dsp@dmdevfs_mockdrv3", "/dev/dmdevfs_mockdrv3"));
    DMOD_TEST_EXPECT_TRUE(wait_ready("inp@dmdevfs_mockdrv4", "/dev/dmdevfs_mockdrv4"));
    DMOD_TEST_EXPECT_EQ(unit_state("inp@dmdevfs_mockdrv3"), -1);    /* display only */
    DMOD_TEST_EXPECT_EQ(unit_state("dsp@dmdevfs_mockdrv4"), -1);    /* input only */
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv3"), -1);
    DMOD_TEST_EXPECT_EQ(unit_state("dsp@dmdevfs_mockdrv5"), -1);    /* report=input */
    DMOD_TEST_EXPECT_EQ(unit_state("inp@dmdevfs_mockdrv5"), -1);    /* ... and no input device */
    DMOD_TEST_EXPECT_EQ(unit_state("dsp@dmdevfs_mockdrv2"), -1);    /* plain node */

    g_fs.deinit(g_mount);
    g_mount = NULL;
    DMOD_TEST_EXPECT_TRUE(wait_state("dsp@dmdevfs_mockdrv3", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_TRUE(wait_state("inp@dmdevfs_mockdrv4", DMOSI_PROCESS_STATE_TERMINATED));
}

/** DMDRVI_IOCTL_DEVFS_GET_FRIEND on @p node through dmdevfs. */
static int get_friend(const char* node, dmdrvi_devfs_friend_t* f)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, node, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return -ENODEV;
    }
    int ret = g_fs.ioctl(g_mount, file, DMDRVI_IOCTL_DEVFS_GET_FRIEND, f);
    g_fs.fclose(g_mount, file);
    return ret;
}

static void ask_friend(dmdrvi_devfs_friend_t* f, uint32_t index, char* path, size_t path_size, char* role, size_t role_size)
{
    memset(f, 0, sizeof(*f));
    f->index = index;
    f->path = path;
    f->path_size = path_size;
    f->role = role;
    f->role_size = role_size;
}

DMOD_TEST_STEP(dmdevfs_finds_friends_of_a_node)
{
    dmdrvi_devfs_friend_t f;
    char path[64], role[16];

    DMOD_TEST_EXPECT_NOT_NULL(g_mount);

    /* NULL buffers: the lengths only */
    ask_friend(&f, 0, NULL, 0, NULL, 0);
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv3", &f), -ERANGE);
    DMOD_TEST_EXPECT_EQ(f.path_length, strlen("/dev/dmdevfs_mockdrv4"));
    DMOD_TEST_EXPECT_EQ(f.role_length, strlen("touch"));

    /* Exactly large enough */
    ask_friend(&f, 0, path, strlen("/dev/dmdevfs_mockdrv4") + 1, role, strlen("touch") + 1);
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv3", &f), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(path, "/dev/dmdevfs_mockdrv4"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(role, "touch"), 0);

    /* One byte short */
    ask_friend(&f, 0, path, strlen("/dev/dmdevfs_mockdrv4"), role, sizeof(role));
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv3", &f), -ERANGE);

    ask_friend(&f, 1, path, sizeof(path), role, sizeof(role));
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv3", &f), -ENOENT);

    ask_friend(&f, 0, path, sizeof(path), role, sizeof(role));
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv4", &f), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(path, "/dev/dmdevfs_mockdrv3"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(role, ""), 0);

    ask_friend(&f, 0, path, sizeof(path), role, sizeof(role));
    DMOD_TEST_EXPECT_EQ(get_friend("/dmdevfs_mockdrv0", &f), -ENOENT);   /* no group */
}

DMOD_TEST_STEP(dmdevfs_reports_hot_plugged_block_node_and_its_removal)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv0", true), 0);
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv0_0", "/dev/dmdevfs_mockdrv0/0"));
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv0_0"), -1);  /* child has no GET_POLICY */

    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv0", false), 0);
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0", DMOSI_PROCESS_STATE_TERMINATED));
}

DMOD_TEST_STEP(dmdevfs_hot_plugged_node_inherits_report_setting)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv1", true), 0);  /* report=none */
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv2", true), 0);  /* default: all */
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv2_0", "/dev/dmdevfs_mockdrv2/0"));
    DMOD_TEST_EXPECT_EQ(unit_state("blk@dmdevfs_mockdrv1_0"), -1);
}

DMOD_TEST_STEP(dmdevfs_opens_only_block_nodes_to_scan_partitions)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv1", true), 0);  /* report=none */
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv2", true), 0);  /* default: all */
    /* One hot-plug thread, in order: mock1's child has been handled once
     * mock2's is reported. */
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv2_0", "/dev/dmdevfs_mockdrv2/0"));

    dmdevfs_mockdrv_stats_t stats;
    /* Plain host node: probed once at mount, never opened again for its
     * partition table since it is no block device - plus get_stats() and
     * set_plugged() themselves. */
    DMOD_TEST_EXPECT_EQ(get_stats("/dmdevfs_mockdrv2", &stats), 0);
    DMOD_TEST_EXPECT_EQ(stats.host_opens, 3u);
    /* report=none: neither the host nor its block child are opened by dmdevfs. */
    DMOD_TEST_EXPECT_EQ(get_stats("/dmdevfs_mockdrv1", &stats), 0);
    DMOD_TEST_EXPECT_EQ(stats.host_opens, 2u);
    DMOD_TEST_EXPECT_EQ(stats.child_opens, 0u);
}

DMOD_TEST_STEP(dmdevfs_reports_removal_of_plugged_node_on_teardown)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv2", true), 0);
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv2_0", "/dev/dmdevfs_mockdrv2/0"));

    g_fs.deinit(g_mount);   /* no unplug: teardown alone must report it */
    g_mount = NULL;
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv2_0", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_TERMINATED));
}
