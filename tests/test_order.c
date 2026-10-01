#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

/*
 * Configuration ordering (driver_order).
 *
 * dmdevfs is mounted at /dev from fixtures/order: several instances of the
 * test driver (mockdrv/) with negative, default and positive driver_order,
 * listed out of order. Every instance records when it was created
 * (DMDEVFS_MOCKDRV_IOCTL_GET_STATS), which gives the order dmdevfs
 * configured them in.
 */

#ifndef DMDEVFS_TEST_FIXTURES_DIR
#define DMDEVFS_TEST_FIXTURES_DIR "fixtures"
#endif

#define HOST(n)     "/dmdevfs_mockdrv" #n

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
    Dmod_Context_t* m = Dmod_GetModuleContext("dmdevfs");
    g_fs.init    = Dmod_GetDifFunction(m, dmod_dmfsi_init_sig);
    g_fs.deinit  = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    g_fs.mounted = Dmod_GetDifFunction(m, dmod_dmfsi_mounted_sig);
    g_fs.fopen   = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    g_fs.fclose  = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    g_fs.ioctl   = Dmod_GetDifFunction(m, dmod_dmfsi_ioctl_sig);
    return g_fs.init && g_fs.deinit && g_fs.mounted && g_fs.fopen && g_fs.fclose && g_fs.ioctl;
}

void dmod_test_setup(void)
{
    g_ready = g_ready || load_devfs();
    g_mount = g_ready ? g_fs.init(DMDEVFS_TEST_FIXTURES_DIR "/order") : NULL;
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

/* When the driver behind @p host was created - 0 if the node is missing. */
static uint32_t create_seq(const char* host)
{
    dmdevfs_mockdrv_stats_t stats = { 0 };
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, host, DMFSI_O_RDWR, 0) != DMFSI_OK)
    {
        return 0;
    }
    int ret = g_fs.ioctl(g_mount, file, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &stats);
    g_fs.fclose(g_mount, file);
    return (ret == 0) ? stats.create_seq : 0;
}

DMOD_TEST_STEP(order_configures_every_driver)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_NE(create_seq(HOST(0)), 0);
    DMOD_TEST_EXPECT_NE(create_seq(HOST(1)), 0);
    DMOD_TEST_EXPECT_NE(create_seq(HOST(2)), 0);
    DMOD_TEST_EXPECT_NE(create_seq(HOST(3)), 0);
    DMOD_TEST_EXPECT_NE(create_seq(HOST(4)), 0);
}

DMOD_TEST_STEP(order_negative_before_default_before_positive)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(create_seq(HOST(3)) < create_seq(HOST(2)));  /* -5 before -1 */
    DMOD_TEST_EXPECT_TRUE(create_seq(HOST(4)) < create_seq(HOST(1)));  /* -1 before  0 */
    DMOD_TEST_EXPECT_TRUE(create_seq(HOST(1)) < create_seq(HOST(0)));  /*  0 before  1 */
}

DMOD_TEST_STEP(order_keeps_file_order_within_negative_value)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_mount);
    DMOD_TEST_EXPECT_TRUE(create_seq(HOST(2)) < create_seq(HOST(4)));  /* both -1 */
}
