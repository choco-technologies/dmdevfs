#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "libsystemd.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>

/*
 * libsystemd node reporting.
 *
 * dmdevfs is mounted at /dev from fixtures/config with the test driver
 * dmdevfs_mockdrv (mockdrv/). libsystemd has the units from fixtures/units
 * and the rules from fixtures/rules loaded, so every report dmdevfs makes
 * starts a real process (dmdevfs_testsvc) as mon@<name> ("monitor") or
 * blk@<name> ("block"), and every removal stops it again - both observable
 * through libsystemd_status(). A unit that was never reported does not exist
 * at all (-ENOENT).
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

DMOD_TEST_STEP(dmdevfs_reports_monitored_node_and_its_removal)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_RUNNING));

    g_fs.deinit(g_mount);
    g_mount = NULL;
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_TERMINATED));
}

DMOD_TEST_STEP(dmdevfs_skips_opted_out_and_unmonitored_nodes)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_RUNNING));
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv1"), -1);    /* report=none */
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv2"), -1);    /* no GET_POLICY */
    DMOD_TEST_EXPECT_EQ(unit_state("blk@dmdevfs_mockdrv0"), -1);    /* host node is no block device */
}

DMOD_TEST_STEP(dmdevfs_reports_hot_plugged_block_node_and_its_removal)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv0", true), 0);
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0", DMOSI_PROCESS_STATE_RUNNING));
    DMOD_TEST_EXPECT_EQ(unit_state("mon@dmdevfs_mockdrv0_0"), -1);  /* child has no GET_POLICY */

    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv0", false), 0);
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0", DMOSI_PROCESS_STATE_TERMINATED));
}

DMOD_TEST_STEP(dmdevfs_hot_plugged_node_inherits_report_setting)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv1", true), 0);  /* report=none */
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv2", true), 0);  /* default: all */
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv2_0", DMOSI_PROCESS_STATE_RUNNING));
    DMOD_TEST_EXPECT_EQ(unit_state("blk@dmdevfs_mockdrv1_0"), -1);
}

DMOD_TEST_STEP(dmdevfs_reports_removal_of_plugged_node_on_teardown)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(set_plugged("/dmdevfs_mockdrv2", true), 0);
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv2_0", DMOSI_PROCESS_STATE_RUNNING));

    g_fs.deinit(g_mount);   /* no unplug: teardown alone must report it */
    g_mount = NULL;
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv2_0", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_TRUE(wait_state("mon@dmdevfs_mockdrv0", DMOSI_PROCESS_STATE_TERMINATED));
}
