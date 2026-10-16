#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>

static dmod_dmfsi_init_t mount_init;
static dmod_dmfsi_deinit_t mount_deinit;
static dmod_dmfsi_mounted_t mounted;
static dmod_dmfsi_fopen_t file_open;
static dmod_dmfsi_fclose_t file_close;
static dmod_dmfsi_ioctl_t file_ioctl;
static dmfsi_context_t mount;
static dmosi_semaphore_t started, finished;
static int unmount_result;

static bool load(void)
{
    if (Dmod_LoadModuleByName("dmdevfs") == NULL || !Dmod_EnableModule("dmdevfs", false, NULL)) return false;
    Dmod_Context_t* m = Dmod_GetModuleContext("dmdevfs");
    mount_init = Dmod_GetDifFunction(m, dmod_dmfsi_init_sig);
    mount_deinit = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    mounted = Dmod_GetDifFunction(m, dmod_dmfsi_mounted_sig);
    file_open = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    file_close = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    file_ioctl = Dmod_GetDifFunction(m, dmod_dmfsi_ioctl_sig);
    return mount_init && mount_deinit && mounted && file_open && file_close && file_ioctl;
}
static void unmount_thread(void* arg)
{
    (void)arg;
    dmosi_semaphore_post(started, 1);
    unmount_result = mount_deinit(mount);
    dmosi_semaphore_post(finished, 1);
}

DMOD_TEST_STEP(unmount_keeps_drivers_alive_until_worker_probe_returns)
{
    DMOD_TEST_EXPECT_TRUE(load());
    if (!mount_init || !mount_deinit) return;
    dmdevfs_mockdrv_probe_gate_t gate = {
        dmosi_semaphore_create(0, 1), dmosi_semaphore_create(0, 1), dmosi_semaphore_create(0, 1)};
    started = dmosi_semaphore_create(0, 1);
    finished = dmosi_semaphore_create(0, 1);
    DMOD_TEST_EXPECT_NOT_NULL(gate.entered); DMOD_TEST_EXPECT_NOT_NULL(gate.release);
    DMOD_TEST_EXPECT_NOT_NULL(gate.freed); DMOD_TEST_EXPECT_NOT_NULL(started);
    DMOD_TEST_EXPECT_NOT_NULL(finished);
    mount = mount_init("fixtures/lifetime");
    DMOD_TEST_EXPECT_NOT_NULL(mount);
    if (mount == NULL) return;
    void* file = NULL;
    DMOD_TEST_EXPECT_EQ(file_open(mount, &file, "/dmdevfs_mockdrv0", DMFSI_O_RDONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_NOT_NULL(file);
    if (file == NULL) { mount_deinit(mount); return; }
    DMOD_TEST_EXPECT_EQ(file_ioctl(mount, file, DMDEVFS_MOCKDRV_IOCTL_PROBE_GATE, &gate), 0);
    file_close(mount, file);
    mounted(mount, "/dev");
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.entered, 1, 2000), 0);

    // A second mount's notification remains queued behind the blocked probe.
    // Unmount must cancel it without waiting for this unrelated worker.
    dmfsi_context_t queued = mount_init("fixtures/lifetime");
    DMOD_TEST_EXPECT_NOT_NULL(queued);
    if (queued != NULL) {
        mounted(queued, "/dev/queued");
        DMOD_TEST_EXPECT_EQ(mount_deinit(queued), DMFSI_OK);
    }

    dmosi_thread_t thread = dmosi_thread_create(unmount_thread, NULL, 1, 16384, "unmount", NULL);
    DMOD_TEST_EXPECT_NOT_NULL(thread);
    if (thread != NULL) {
        DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(started, 1, 2000), 0);
        DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(finished, 1, 50), -ETIMEDOUT);
        DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 10), -ETIMEDOUT);
    }
    dmosi_semaphore_post(gate.release, 1);
    if (thread != NULL) {
        DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(finished, 1, 2000), 0);
        DMOD_TEST_EXPECT_EQ(dmosi_thread_join(thread), 0);
        dmosi_thread_destroy(thread);
        DMOD_TEST_EXPECT_EQ(unmount_result, DMFSI_OK);
    } else mount_deinit(mount);
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 2000), 0);
    dmosi_semaphore_destroy(gate.entered); dmosi_semaphore_destroy(gate.release);
    dmosi_semaphore_destroy(gate.freed); dmosi_semaphore_destroy(started);
    dmosi_semaphore_destroy(finished);
}
