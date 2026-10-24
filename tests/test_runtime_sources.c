#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmosi.h"
#include "dmdevfs.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

#define BASE "fixtures/runtime/base"
#define EXT "fixtures/runtime/ext"
#define SD "fixtures/runtime/sd"
#define HOST(n) "/dmdevfs_mockdrv" #n

static dmod_dmfsi_init_t fs_init;
static dmod_dmfsi_deinit_t fs_deinit;
static dmod_dmfsi_mounted_t fs_mounted;
static dmod_dmfsi_fopen_t fs_open;
static dmod_dmfsi_fclose_t fs_close;
static dmod_dmfsi_ioctl_t fs_ioctl;
static dmod_dmfsi_fread_t fs_read;
static dmod_dmfsi_stat_t fs_stat;
static dmod_dmfsi_opendir_t fs_opendir;
static dmod_dmfsi_readdir_t fs_readdir;
static dmod_dmfsi_closedir_t fs_closedir;
static dmfsi_context_t mount;
static void* control;

static bool load(void)
{
    if (Dmod_LoadModuleByName("dmdevfs") == NULL || !Dmod_EnableModule("dmdevfs", false, NULL)) return false;
    Dmod_Context_t* m = Dmod_GetModuleContext("dmdevfs");
    fs_init = Dmod_GetDifFunction(m, dmod_dmfsi_init_sig);
    fs_deinit = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    fs_mounted = Dmod_GetDifFunction(m, dmod_dmfsi_mounted_sig);
    fs_open = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    fs_close = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    fs_ioctl = Dmod_GetDifFunction(m, dmod_dmfsi_ioctl_sig);
    fs_read = Dmod_GetDifFunction(m, dmod_dmfsi_fread_sig);
    fs_stat = Dmod_GetDifFunction(m, dmod_dmfsi_stat_sig);
    fs_opendir = Dmod_GetDifFunction(m, dmod_dmfsi_opendir_sig);
    fs_readdir = Dmod_GetDifFunction(m, dmod_dmfsi_readdir_sig);
    fs_closedir = Dmod_GetDifFunction(m, dmod_dmfsi_closedir_sig);
    return fs_init && fs_deinit && fs_mounted && fs_open && fs_close && fs_ioctl && fs_read && fs_stat && fs_opendir && fs_readdir && fs_closedir;
}
void dmod_test_setup(void)
{
    control = NULL;
    mount = load() ? fs_init(BASE) : NULL;
    if (mount != NULL)
    {
        fs_mounted(mount, "/dev");
        fs_open(mount, &control, "/null", DMFSI_O_RDWR, 0);
    }
}
void dmod_test_teardown(void)
{
    if (control) fs_close(mount, control);
    if (mount) DMOD_TEST_EXPECT_EQ(fs_deinit(mount), 0);
    mount = NULL;
}
static int command(int request, const char* path)
{
    return fs_ioctl(mount, control, request, (void*)path);
}
static bool exists(const char* path)
{
    dmfsi_stat_t st = {0};
    return fs_stat(mount, path, &st) == 0;
}
static int node_command(const char* path, int request, void* arg)
{
    void* f = NULL;
    int ret = fs_open(mount, &f, path, DMFSI_O_RDWR, 0);
    if (ret != 0) return ret;
    ret = fs_ioctl(mount, f, request, arg);
    fs_close(mount, f);
    return ret;
}
static bool wait_node(const char* path)
{
    for (int i = 0; i < 100; ++i)
    {
        if (exists(path)) return true;
        dmosi_thread_sleep(10);
    }
    return false;
}

DMOD_TEST_STEP(runtime_sources_coexist_and_remove_independently)
{
    DMOD_TEST_EXPECT_NOT_NULL(control);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, SD), 0);
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(40)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(41)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(43)));
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(41)));
    DMOD_TEST_EXPECT_EQ(node_command(HOST(0), DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &(dmdevfs_mockdrv_stats_t){0}), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, SD), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(41)));
    DMOD_TEST_EXPECT_FALSE(exists(HOST(43)));
}
DMOD_TEST_STEP(runtime_sources_normalize_and_reject_invalid_requests)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, "./fixtures//runtime/ext/./"), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), -EEXIST);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, "fixtures/runtime"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT "/sub"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, ""), -EINVAL);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT "/../sd"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, "fixtures/does-not-exist"), -EIO);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, "fixtures/does-not-exist"), -ENOENT);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT "/"), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), -ENOENT);
}
DMOD_TEST_STEP(runtime_sources_rollback_partial_add_and_preserve_original_nodes)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, "fixtures/runtime/collision"), -EIO);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(42)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
    DMOD_TEST_EXPECT_EQ(node_command(HOST(0), DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &(dmdevfs_mockdrv_stats_t){0}), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, "fixtures/runtime/bad"), -EIO);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(42)));
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
}
DMOD_TEST_STEP(runtime_sources_remove_children_and_defer_driver_free_until_last_close)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(HOST(40) "/0"));
    void *host = NULL, *child = NULL;
    DMOD_TEST_EXPECT_EQ(fs_open(mount, &host, HOST(40), DMFSI_O_RDWR, 0), 0);
    DMOD_TEST_EXPECT_EQ(fs_open(mount, &child, HOST(40) "/0", DMFSI_O_RDWR, 0), 0);
    dmdevfs_mockdrv_probe_gate_t gate = {0};
    gate.freed = dmosi_semaphore_create(0, 1);
    DMOD_TEST_EXPECT_EQ(fs_ioctl(mount, host, DMDEVFS_MOCKDRV_IOCTL_PROBE_GATE, &gate), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40)));
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40) "/0"));
    DMOD_TEST_EXPECT_NE(dmosi_semaphore_wait(gate.freed, 1, 0), 0);
    uint8_t byte = 0; size_t read = 99;
    DMOD_TEST_EXPECT_EQ(fs_read(mount, child, &byte, 1, &read), -ENODEV);
    DMOD_TEST_EXPECT_EQ(read, 0);
    DMOD_TEST_EXPECT_EQ(fs_ioctl(mount, host, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &(dmdevfs_mockdrv_stats_t){0}), -ENODEV);
    DMOD_TEST_EXPECT_EQ(fs_close(mount, host), 0);
    DMOD_TEST_EXPECT_NE(dmosi_semaphore_wait(gate.freed, 1, 0), 0);
    DMOD_TEST_EXPECT_EQ(fs_close(mount, child), 0);
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 2000), 0);
    dmosi_semaphore_destroy(gate.freed);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_TRUE(exists(HOST(40)));
}
DMOD_TEST_STEP(runtime_sources_can_remove_boot_source_and_repeat_cycles)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, BASE), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(0)));
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &(dmdevfs_mockdrv_stats_t){0}), 0);
    for (int i = 0; i < 10; ++i)
    {
        DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, SD), 0);
        DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, SD), 0);
    }
    DMOD_TEST_EXPECT_TRUE(exists(HOST(40)));
    DMOD_TEST_EXPECT_TRUE(exists("/null"));
}

DMOD_TEST_STEP(runtime_sources_remove_when_configuration_files_are_gone)
{
    const char* dir = "runtime-temporary";
    const char* ini = "runtime-temporary/device.ini";
    DMOD_TEST_EXPECT_EQ(Dmod_MakeDir(dir, 0755), 0);
    void* file = Dmod_FileOpen(ini, "w");
    DMOD_TEST_EXPECT_NOT_NULL(file);
    if (file == NULL) return;
    const char* config = "[device]\ndriver_name=dmdevfs_mockdrv\nmajor=44\n";
    DMOD_TEST_EXPECT_EQ(Dmod_FileWrite(config, 1, strlen(config), file), strlen(config));
    Dmod_FileClose(file);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, dir), 0);
    DMOD_TEST_EXPECT_TRUE(exists(HOST(44)));
    DMOD_TEST_EXPECT_EQ(Dmod_FileRemove(ini), 0);
    DMOD_TEST_EXPECT_EQ(Dmod_RemoveDir(dir), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, dir), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(44)));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
}
DMOD_TEST_STEP(runtime_sources_invalidate_already_unplugged_child_handles)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(HOST(40) "/0"));
    void* child = NULL;
    DMOD_TEST_EXPECT_EQ(fs_open(mount, &child, HOST(40) "/0", DMFSI_O_RDWR, 0), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_UNPLUG, NULL), 0);
    // FIFO: the following source command is a barrier for UNPLUG.
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    size_t done = 9; uint8_t byte = 0;
    DMOD_TEST_EXPECT_EQ(fs_read(mount, child, &byte, 1, &done), -ENODEV);
    DMOD_TEST_EXPECT_EQ(done, 0);
    DMOD_TEST_EXPECT_EQ(fs_close(mount, child), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    // Teardown must not retain a reference from the unplug callback.
    DMOD_TEST_EXPECT_EQ(fs_close(mount, control), 0);
    control = NULL;
    int ret = fs_deinit(mount);
    DMOD_TEST_EXPECT_EQ(ret, 0);
    if (ret == 0) mount = NULL;
}

DMOD_TEST_STEP(runtime_sources_remove_partition_descendants)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    uint64_t blocks = 64;
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_SET_MEDIA, &blocks), 0);
    dmdevfs_mockdrv_sector_t mbr = {0};
    mbr.data[446 + 4] = 0x83;
    mbr.data[446 + 8] = 1;
    mbr.data[446 + 12] = 16;
    mbr.data[510] = 0x55;
    mbr.data[511] = 0xaa;
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR, &mbr), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(HOST(40) "/0p1"));
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40) "/0p1"));
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40) "/0"));
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
}

DMOD_TEST_STEP(runtime_sources_open_directory_survives_source_removal)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    dmdevfs_mockdrv_probe_gate_t gate = {0};
    gate.freed = dmosi_semaphore_create(0, 1);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_PROBE_GATE, &gate), 0);
    void* dir = NULL;
    DMOD_TEST_EXPECT_EQ(fs_opendir(mount, &dir, "/"), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_NE(dmosi_semaphore_wait(gate.freed, 1, 0), 0);
    dmfsi_dir_entry_t entry;
    unsigned entries = 0;
    while (fs_readdir(mount, dir, &entry) == 0 && entries < 10)
    {
        DMOD_TEST_EXPECT_TRUE(strcmp(entry.name, "dmdevfs_mockdrv40") != 0);
        ++entries;
    }
    DMOD_TEST_EXPECT_EQ(entries, 2);
    DMOD_TEST_EXPECT_EQ(fs_closedir(mount, dir), 0);
    DMOD_TEST_EXPECT_EQ(dmosi_semaphore_wait(gate.freed, 1, 2000), 0);
    dmosi_semaphore_destroy(gate.freed);
}

DMOD_TEST_STEP(runtime_sources_readd_while_old_handles_are_open)
{
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(wait_node(HOST(40) "/0"));
    void* old = NULL;
    DMOD_TEST_EXPECT_EQ(fs_open(mount, &old, HOST(40), DMFSI_O_RDWR, 0), 0);
    dmdevfs_mockdrv_stats_t before = {0}, after = {0};
    DMOD_TEST_EXPECT_EQ(fs_ioctl(mount, old, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &before), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);

    // Withdrawn nodes still held by old handles must be invisible in a
    // newly opened directory, including the old child's parent directory.
    void* dir = NULL;
    DMOD_TEST_EXPECT_EQ(fs_opendir(mount, &dir, "/"), 0);
    dmfsi_dir_entry_t entry;
    unsigned entries = 0;
    while (fs_readdir(mount, dir, &entry) == 0 && entries < 10)
    {
        DMOD_TEST_EXPECT_TRUE(strcmp(entry.name, "dmdevfs_mockdrv40") != 0);
        ++entries;
    }
    DMOD_TEST_EXPECT_EQ(entries, 2);
    DMOD_TEST_EXPECT_EQ(fs_closedir(mount, dir), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_ADD_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_EQ(node_command(HOST(40), DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &after), 0);
    DMOD_TEST_EXPECT_TRUE(after.create_seq > before.create_seq);
    DMOD_TEST_EXPECT_FALSE(exists(HOST(40) "/0"));
    DMOD_TEST_EXPECT_EQ(fs_ioctl(mount, old, DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &before), -ENODEV);
    DMOD_TEST_EXPECT_EQ(fs_close(mount, old), 0);
    DMOD_TEST_EXPECT_EQ(command(DMDEVFS_IOCTL_REMOVE_CONFIG, EXT), 0);
    DMOD_TEST_EXPECT_TRUE(exists(HOST(0)));
}
