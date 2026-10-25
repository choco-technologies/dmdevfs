#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "dmdevfs_cfg.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

/*
 * Configuration directories of mount points (dmdevfs_add_config_dir()).
 *
 * dmdevfs is mounted at /dev from fixtures/cfgbase/cfg/drivers (the test
 * driver's instance with major 20). fixtures/cfgmount stands for a mounted
 * file system whose cfg/drivers configures major 21, fixtures/cfgnone for one
 * without a configuration directory. Loading and unloading are queued for
 * dmdevfs's hot-plug thread, so the steps wait for their effect.
 */

#ifndef DMDEVFS_TEST_FIXTURES_DIR
#define DMDEVFS_TEST_FIXTURES_DIR "fixtures"
#endif

#define BASE_MOUNT      DMDEVFS_TEST_FIXTURES_DIR "/cfgbase"
#define CARD_MOUNT      DMDEVFS_TEST_FIXTURES_DIR "/cfgmount"
#define NONE_MOUNT      DMDEVFS_TEST_FIXTURES_DIR "/cfgnone"
#define BASE_HOST       "/dmdevfs_mockdrv20"
#define CARD_HOST       "/dmdevfs_mockdrv21"
#define CARD_CHILD      "/dmdevfs_mockdrv21/0"
#define WAIT_MS         2000

typedef struct
{
    dmod_dmfsi_init_t       init;
    dmod_dmfsi_deinit_t     deinit;
    dmod_dmfsi_mounted_t    mounted;
    dmod_dmfsi_fopen_t      fopen;
    dmod_dmfsi_fclose_t     fclose;
    dmod_dmfsi_ioctl_t      ioctl;
    dmod_dmfsi_opendir_t    opendir;
    dmod_dmfsi_readdir_t    readdir;
    dmod_dmfsi_closedir_t   closedir;
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
    Dmod_Context_t* m = Dmod_GetModuleContext("dmdevfs");
    g_fs.init     = Dmod_GetDifFunction(m, dmod_dmfsi_init_sig);
    g_fs.deinit   = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    g_fs.mounted  = Dmod_GetDifFunction(m, dmod_dmfsi_mounted_sig);
    g_fs.fopen    = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    g_fs.fclose   = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    g_fs.ioctl    = Dmod_GetDifFunction(m, dmod_dmfsi_ioctl_sig);
    g_fs.opendir  = Dmod_GetDifFunction(m, dmod_dmfsi_opendir_sig);
    g_fs.readdir  = Dmod_GetDifFunction(m, dmod_dmfsi_readdir_sig);
    g_fs.closedir = Dmod_GetDifFunction(m, dmod_dmfsi_closedir_sig);
    return g_fs.init && g_fs.deinit && g_fs.mounted && g_fs.fopen && g_fs.fclose && g_fs.ioctl &&
           g_fs.opendir && g_fs.readdir && g_fs.closedir;
}

void dmod_test_setup(void)
{
    g_ready = g_ready || load_devfs();
    g_mount = g_ready ? g_fs.init(BASE_MOUNT "/cfg/drivers") : NULL;
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

static bool node_exists(const char* path)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, path, DMFSI_O_RDWR, 0) != DMFSI_OK)
    {
        return false;
    }
    g_fs.fclose(g_mount, file);
    return true;
}

static bool wait_node(const char* path, bool exists)
{
    for (int waited = 0; waited < WAIT_MS; waited += 10)
    {
        if (node_exists(path) == exists)
        {
            return true;
        }
        dmosi_thread_sleep(10);
    }
    return false;
}

/* Number of nodes at the root of the mount */
static int count_nodes(void)
{
    void* dir = NULL;
    if (g_fs.opendir(g_mount, &dir, "/") != DMFSI_OK)
    {
        return -1;
    }
    int count = 0;
    dmfsi_dir_entry_t entry;
    while (g_fs.readdir(g_mount, dir, &entry) == DMFSI_OK)
    {
        count++;
    }
    g_fs.closedir(g_mount, dir);
    return count;
}

DMOD_TEST_STEP(config_dir_rejects_null_mount_point)
{
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(NULL), -EINVAL);
}

DMOD_TEST_STEP(config_dir_drivers_are_configured_and_removed)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(node_exists(BASE_HOST));
    DMOD_TEST_EXPECT_FALSE(node_exists(CARD_HOST));

    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, true));

    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, false));
    DMOD_TEST_EXPECT_TRUE(node_exists(BASE_HOST));
}

DMOD_TEST_STEP(config_dir_is_configured_once)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    int nodes = count_nodes();

    // The mount's own directory, a directory loaded twice, a mount point without one
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(BASE_MOUNT), 0);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(NONE_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, true));
    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(NONE_MOUNT), 0);
    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(BASE_MOUNT), 0);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(NONE_MOUNT), 0);   // queued after the rest: all of it is done once this is
    for (int waited = 0; waited < WAIT_MS && count_nodes() != nodes + 1; waited += 10)
    {
        dmosi_thread_sleep(10);
    }
    DMOD_TEST_EXPECT_EQ(count_nodes(), nodes + 1);
    DMOD_TEST_EXPECT_TRUE(node_exists(BASE_HOST));

    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, false));
    DMOD_TEST_EXPECT_EQ(count_nodes(), nodes);
}

DMOD_TEST_STEP(config_dir_driver_is_freed_after_its_last_handle)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, true));

    dmdevfs_mockdrv_probe_gate_t gate = { NULL, NULL, dmosi_semaphore_create(0, 1) };
    DMOD_TEST_EXPECT_NOT_NULL(gate.freed);
    void* file = NULL;
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_mount, &file, CARD_HOST, DMFSI_O_RDWR, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_PROBE_GATE, &gate), 0);

    // Unpublished, but the open handle keeps the driver
    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, false));
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 50), -ETIMEDOUT);
    dmdevfs_mockdrv_stats_t stats = { 0 };
    DMOD_TEST_EXPECT_EQ(g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &stats), 0);

    // Its last handle closed: freed
    g_fs.fclose(g_mount, file);
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, WAIT_MS), 0);
    dmosi_semaphore_destroy(gate.freed);
}

DMOD_TEST_STEP(config_dir_driver_goes_with_its_hot_plugged_devices)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_EQ(dmdevfs_add_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, true));

    dmdevfs_mockdrv_probe_gate_t gate = { NULL, NULL, dmosi_semaphore_create(0, 1) };
    void* file = NULL;
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_mount, &file, CARD_HOST, DMFSI_O_RDWR, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_PROBE_GATE, &gate), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL), 0);
    g_fs.fclose(g_mount, file);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_CHILD, true));

    // A handle on the child keeps the (shared) driver context alive
    void* child = NULL;
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_mount, &child, CARD_CHILD, DMFSI_O_RDWR, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(dmdevfs_remove_config_dir(CARD_MOUNT), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(CARD_HOST, false));
    DMOD_TEST_EXPECT_FALSE(node_exists(CARD_CHILD));
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 50), -ETIMEDOUT);

    g_fs.fclose(g_mount, child);
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, WAIT_MS), 0);
    dmosi_semaphore_destroy(gate.freed);
}
