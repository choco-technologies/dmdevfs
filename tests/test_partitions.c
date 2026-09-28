#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmdrvi_ioctl.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "libsystemd.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

/*
 * Partition nodes.
 *
 * dmdevfs is mounted at /dev from fixtures/partitions with the test driver
 * (mockdrv/). Each step writes a partition table onto the (sparse) medium of
 * the driver's child through the host node, plugs the child in - dmdevfs
 * then scans /dmdevfs_mockdrv0/0 and exposes /dmdevfs_mockdrv0/0p<N> - and
 * checks the nodes through the dmfsi functions.
 */

#ifndef DMDEVFS_TEST_FIXTURES_DIR
#define DMDEVFS_TEST_FIXTURES_DIR "fixtures"
#endif

#define HOST        "/dmdevfs_mockdrv0"
#define DISK        "/dmdevfs_mockdrv0/0"
#define PART(n)     "/dmdevfs_mockdrv0/0p" #n
#define BLOCK       DMDEVFS_MOCKDRV_BLOCK_SIZE
#define WAIT_MS     3000

typedef struct
{
    dmod_dmfsi_init_t       init;
    dmod_dmfsi_deinit_t     deinit;
    dmod_dmfsi_mounted_t    mounted;
    dmod_dmfsi_fopen_t      fopen;
    dmod_dmfsi_fclose_t     fclose;
    dmod_dmfsi_fread_t      fread;
    dmod_dmfsi_fwrite_t     fwrite;
    dmod_dmfsi_lseek_t      lseek;
    dmod_dmfsi_ioctl_t      ioctl;
    dmod_dmfsi_stat_t       stat;
} devfs_t;

typedef struct
{
    uint8_t     type;
    uint32_t    first_lba;
    uint32_t    lba_count;
} mbr_part_t;

static devfs_t          g_fs;
static bool             g_ready;
static dmfsi_context_t  g_mount;
static uint8_t          g_sector[BLOCK];

/* ======================================================================
 *  dmdevfs access
 * ====================================================================== */

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
    g_fs.fread   = Dmod_GetDifFunction(m, dmod_dmfsi_fread_sig);
    g_fs.fwrite  = Dmod_GetDifFunction(m, dmod_dmfsi_fwrite_sig);
    g_fs.lseek   = Dmod_GetDifFunction(m, dmod_dmfsi_lseek_sig);
    g_fs.ioctl   = Dmod_GetDifFunction(m, dmod_dmfsi_ioctl_sig);
    g_fs.stat    = Dmod_GetDifFunction(m, dmod_dmfsi_stat_sig);
    return g_fs.init && g_fs.deinit && g_fs.mounted && g_fs.fopen && g_fs.fclose && g_fs.fread &&
           g_fs.fwrite && g_fs.lseek && g_fs.ioctl && g_fs.stat;
}

static int node_ioctl(const char* path, int command, void* arg)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, path, DMFSI_O_RDWR, 0) != DMFSI_OK)
    {
        return -ENOENT;
    }
    int ret = g_fs.ioctl(g_mount, file, command, arg);
    g_fs.fclose(g_mount, file);
    return ret;
}

void dmod_test_setup(void)
{
    g_ready = g_ready || load_devfs();
    g_mount = g_ready ? g_fs.init(DMDEVFS_TEST_FIXTURES_DIR "/partitions") : NULL;
    if (g_mount != NULL)
    {
        g_fs.mounted(g_mount, "/dev");
    }
}

void dmod_test_teardown(void)
{
    if (g_mount != NULL)
    {
        node_ioctl(HOST, DMDEVFS_MOCKDRV_IOCTL_UNPLUG, NULL);
        g_fs.deinit(g_mount);
        g_mount = NULL;
    }
}

static bool exists(const char* path)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, path, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return false;
    }
    g_fs.fclose(g_mount, file);
    return true;
}

static bool wait_exists(const char* path, bool present)
{
    for (int waited = 0; waited < WAIT_MS; waited += 5)
    {
        if (exists(path) == present)
        {
            return true;
        }
        dmosi_thread_sleep(5);
    }
    return false;
}

static uint64_t node_size(const char* path)
{
    dmfsi_stat_t stat;
    memset(&stat, 0, sizeof(stat));
    return (g_fs.stat(g_mount, path, &stat) == DMFSI_OK) ? (uint64_t)stat.size : UINT64_MAX;
}

/** Read or write @p size bytes at @p offset of a node; returns bytes transferred or -1. */
static int64_t transfer(const char* path, uint64_t offset, void* buffer, size_t size, bool write)
{
    void* file = NULL;
    if (g_fs.fopen(g_mount, &file, path, DMFSI_O_RDWR, 0) != DMFSI_OK)
    {
        return -1;
    }
    size_t done = 0;
    int ret = DMFSI_ERR_GENERAL;
    if (g_fs.lseek(g_mount, file, (dmfsi_offset_t)offset, DMFSI_SEEK_SET) == (dmfsi_offset_t)offset)
    {
        ret = write ? g_fs.fwrite(g_mount, file, buffer, size, &done)
                    : g_fs.fread(g_mount, file, buffer, size, &done);
    }
    g_fs.fclose(g_mount, file);
    return (ret == DMFSI_OK) ? (int64_t)done : -1;
}

/* ======================================================================
 *  Media images
 * ====================================================================== */

static void put_le32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void put_le64(uint8_t* p, uint64_t v)
{
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

/* memcmp() is not exported to modules */
static bool bytes_equal(const void* a, const void* b, size_t size)
{
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < size; i++)
    {
        if (pa[i] != pb[i])
        {
            return false;
        }
    }
    return true;
}

static uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool write_sector(uint64_t lba, const uint8_t* data)
{
    dmdevfs_mockdrv_sector_t sector;
    sector.lba = lba;
    memcpy(sector.data, data, BLOCK);
    return node_ioctl(HOST, DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR, &sector) == 0;
}

static bool set_media(uint64_t blocks)
{
    return node_ioctl(HOST, DMDEVFS_MOCKDRV_IOCTL_SET_MEDIA, &blocks) == 0;
}

static bool plug(void)
{
    return node_ioctl(HOST, DMDEVFS_MOCKDRV_IOCTL_PLUG, NULL) == 0 && wait_exists(DISK, true);
}

/** Build an MBR (or EBR) sector with up to four entries. */
static void build_mbr(uint8_t* sector, const mbr_part_t* parts, int count)
{
    memset(sector, 0, BLOCK);
    for (int i = 0; i < count; i++)
    {
        uint8_t* e = sector + 446 + 16 * i;
        e[4] = parts[i].type;
        put_le32(e + 8, parts[i].first_lba);
        put_le32(e + 12, parts[i].lba_count);
    }
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

static bool write_mbr(const mbr_part_t* parts, int count)
{
    build_mbr(g_sector, parts, count);
    return write_sector(0, g_sector);
}

/** GPT entry slots used by the steps: 4 entries of 128 bytes, one sector. */
#define GPT_ENTRIES     4u

typedef struct
{
    bool        used;
    uint64_t    first;
    uint64_t    last;
} gpt_part_t;

static void build_gpt_entries(uint8_t* sector, const gpt_part_t* parts)
{
    memset(sector, 0, BLOCK);
    for (uint32_t i = 0; i < GPT_ENTRIES; i++)
    {
        if (parts[i].used)
        {
            uint8_t* e = sector + 128u * i;
            e[0] = 0xAF;                    /* any non-zero type GUID */
            e[16] = (uint8_t)(i + 1u);      /* unique GUID */
            put_le64(e + 32, parts[i].first);
            put_le64(e + 40, parts[i].last);
        }
    }
}

static void build_gpt_header(uint8_t* sector, uint64_t my_lba, uint64_t alt_lba, uint64_t entries_lba,
                             uint64_t blocks, uint32_t entries_crc)
{
    memset(sector, 0, BLOCK);
    memcpy(sector, "EFI PART", 8);
    put_le32(sector + 8, 0x00010000u);
    put_le32(sector + 12, 92);
    put_le64(sector + 24, my_lba);
    put_le64(sector + 32, alt_lba);
    put_le64(sector + 40, 34);
    put_le64(sector + 48, blocks - 34);
    put_le64(sector + 72, entries_lba);
    put_le32(sector + 80, GPT_ENTRIES);
    put_le32(sector + 84, 128);
    put_le32(sector + 88, entries_crc);
    put_le32(sector + 16, crc32(sector, 92));
}

/** Protective MBR, primary GPT (LBA 1-2) and backup GPT (last two LBAs). */
static bool write_gpt(uint64_t blocks, const gpt_part_t* parts)
{
    uint8_t entries[BLOCK];
    build_gpt_entries(entries, parts);
    uint32_t entries_crc = crc32(entries, GPT_ENTRIES * 128u);
    uint64_t last = blocks - 1u;
    mbr_part_t protective = { 0xEE, 1, (blocks - 1u > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)(blocks - 1u) };

    bool ok = write_mbr(&protective, 1);
    build_gpt_header(g_sector, 1, last, 2, blocks, entries_crc);
    ok = ok && write_sector(1, g_sector) && write_sector(2, entries);
    build_gpt_header(g_sector, last, 1, last - 1u, blocks, entries_crc);
    return ok && write_sector(last - 1u, entries) && write_sector(last, g_sector);
}

/* ======================================================================
 *  MBR
 * ====================================================================== */

DMOD_TEST_STEP(partitions_mbr_with_one_primary)
{
    mbr_part_t parts[] = { { 0x0C, 2048, 8192 } };
    DMOD_TEST_EXPECT_TRUE(set_media(16384) && write_mbr(parts, 1) && plug());
    DMOD_TEST_EXPECT_TRUE(wait_exists(PART(1), true));
    DMOD_TEST_EXPECT_EQ(node_size(PART(1)), 8192ull * BLOCK);
    DMOD_TEST_EXPECT_FALSE(exists(PART(2)));
    DMOD_TEST_EXPECT_EQ(node_size(DISK), 16384ull * BLOCK);    /* the whole disk stays */
}

DMOD_TEST_STEP(partitions_mbr_with_four_primaries)
{
    mbr_part_t parts[] = { { 0x83, 2048, 1024 }, { 0x83, 4096, 2048 }, { 0x07, 8192, 512 }, { 0x0C, 9000, 100 } };
    DMOD_TEST_EXPECT_TRUE(set_media(16384) && write_mbr(parts, 4) && plug());
    DMOD_TEST_EXPECT_TRUE(wait_exists(PART(4), true));
    DMOD_TEST_EXPECT_EQ(node_size(PART(1)), 1024ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(2)), 2048ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(3)), 512ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(4)), 100ull * BLOCK);
    DMOD_TEST_EXPECT_FALSE(exists(PART(5)));
}

DMOD_TEST_STEP(partitions_mbr_extended_with_logical_partitions)
{
    /* p1 primary, slot 2 extended 4096..12287 holding two logical partitions */
    mbr_part_t primary[] = { { 0x83, 2048, 2048 }, { 0x0F, 4096, 8192 } };
    mbr_part_t ebr1[]    = { { 0x83, 2048, 1000 }, { 0x05, 4096, 4096 } };  /* -> 6144; next EBR at 8192 */
    mbr_part_t ebr2[]    = { { 0x83, 2048, 1000 } };                        /* -> 10240 */
    uint8_t sector[BLOCK];
    bool ok = set_media(16384) && write_mbr(primary, 2);
    build_mbr(sector, ebr1, 2);
    ok = ok && write_sector(4096, sector);
    build_mbr(sector, ebr2, 1);
    ok = ok && write_sector(8192, sector);
    DMOD_TEST_EXPECT_TRUE(ok && plug());

    DMOD_TEST_EXPECT_TRUE(wait_exists(PART(6), true));
    DMOD_TEST_EXPECT_EQ(node_size(PART(1)), 2048ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(5)), 1000ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(6)), 1000ull * BLOCK);
    DMOD_TEST_EXPECT_FALSE(exists(PART(2)));        /* the container itself is no partition node */
    DMOD_TEST_EXPECT_FALSE(exists(PART(7)));

    /* p6 really starts at 10240 */
    uint8_t mark[4] = { 'p', '6', '!', '!' };
    uint8_t back[4] = { 0 };
    DMOD_TEST_EXPECT_EQ(transfer(PART(6), 0, mark, sizeof(mark), true), 4);
    DMOD_TEST_EXPECT_EQ(transfer(DISK, 10240ull * BLOCK, back, sizeof(back), false), 4);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(mark, back, sizeof(mark)));
}

DMOD_TEST_STEP(partitions_superfloppy_has_none)
{
    uint8_t boot[BLOCK];
    memset(boot, 0, sizeof(boot));
    boot[0] = 0xEB; boot[1] = 0x3C; boot[2] = 0x90;
    memcpy(boot + 3, "MSDOS5.0", 8);
    boot[11] = 0x00; boot[12] = 0x02;               /* 512 bytes per sector */
    memcpy(boot + 54, "FAT16   ", 8);
    boot[510] = 0x55; boot[511] = 0xAA;
    DMOD_TEST_EXPECT_TRUE(set_media(16384) && write_sector(0, boot) && plug());
    dmosi_thread_sleep(100);
    DMOD_TEST_EXPECT_TRUE(exists(DISK));
    DMOD_TEST_EXPECT_FALSE(exists(PART(1)));
}

DMOD_TEST_STEP(partitions_blank_medium_has_none)
{
    DMOD_TEST_EXPECT_TRUE(set_media(16384) && plug());
    dmosi_thread_sleep(100);
    DMOD_TEST_EXPECT_FALSE(exists(PART(1)));
}

/* ======================================================================
 *  GPT
 * ====================================================================== */

DMOD_TEST_STEP(partitions_gpt_with_several_entries)
{
    gpt_part_t parts[GPT_ENTRIES] = { { true, 2048, 4095 }, { true, 4096, 8191 }, { false, 0, 0 }, { true, 8192, 16383 } };
    DMOD_TEST_EXPECT_TRUE(set_media(65536) && write_gpt(65536, parts) && plug());
    DMOD_TEST_EXPECT_TRUE(wait_exists(PART(4), true));
    DMOD_TEST_EXPECT_EQ(node_size(PART(1)), 2048ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(2)), 4096ull * BLOCK);
    DMOD_TEST_EXPECT_EQ(node_size(PART(4)), 8192ull * BLOCK);
    DMOD_TEST_EXPECT_FALSE(exists(PART(3)));        /* empty entry */
}

DMOD_TEST_STEP(partitions_gpt_backup_replaces_corrupted_primary)
{
    gpt_part_t parts[GPT_ENTRIES] = { { true, 2048, 4095 }, { true, 4096, 8191 } };
    uint8_t header[BLOCK];
    bool ok = set_media(65536) && write_gpt(65536, parts);
    build_gpt_header(header, 1, 65535, 2, 65536, 0);
    header[40] ^= 0xFF;                             /* first_usable changed after the CRC */
    DMOD_TEST_EXPECT_TRUE(ok && write_sector(1, header) && plug());
    DMOD_TEST_EXPECT_TRUE(wait_exists(PART(2), true));
    DMOD_TEST_EXPECT_EQ(node_size(PART(2)), 4096ull * BLOCK);
}

DMOD_TEST_STEP(partitions_gpt_with_both_headers_corrupted_has_none)
{
    gpt_part_t parts[GPT_ENTRIES] = { { true, 2048, 4095 } };
    uint8_t broken[BLOCK];
    memset(broken, 0xA5, sizeof(broken));
    DMOD_TEST_EXPECT_TRUE(set_media(65536) && write_gpt(65536, parts) &&
                          write_sector(1, broken) && write_sector(65535, broken) && plug());
    dmosi_thread_sleep(100);
    DMOD_TEST_EXPECT_FALSE(exists(PART(1)));
}

/* ======================================================================
 *  I/O through a partition node (64-bit)
 * ====================================================================== */

#define BIG_BLOCKS      (20ull * 1024 * 1024 * 1024 / BLOCK)    /* 20 GiB */
#define BIG_FIRST       (5ull * 1024 * 1024 * 1024 / BLOCK)     /* starts at 5 GiB */
#define BIG_COUNT       2048ull                                 /* 1 MiB */
#define BIG_LENGTH      (BIG_COUNT * BLOCK)

static bool plug_big_gpt(void)
{
    gpt_part_t parts[GPT_ENTRIES] = { { true, BIG_FIRST, BIG_FIRST + BIG_COUNT - 1u } };
    return set_media(BIG_BLOCKS) && write_gpt(BIG_BLOCKS, parts) && plug() && wait_exists(PART(1), true);
}

DMOD_TEST_STEP(partitions_io_is_shifted_above_4gib)
{
    DMOD_TEST_EXPECT_TRUE(plug_big_gpt());
    DMOD_TEST_EXPECT_EQ(node_size(PART(1)), BIG_LENGTH);

    uint8_t data[9] = { 'p', 'a', 'r', 't', 'i', 't', 'i', 'o', 'n' };
    uint8_t back[9] = { 0 };
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), 4096, data, sizeof(data), true), (int64_t)sizeof(data));
    DMOD_TEST_EXPECT_EQ(transfer(DISK, BIG_FIRST * BLOCK + 4096, back, sizeof(back), false), (int64_t)sizeof(back));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(data, back, sizeof(data)));
}

DMOD_TEST_STEP(partitions_io_is_clipped_at_the_partition_end)
{
    DMOD_TEST_EXPECT_TRUE(plug_big_gpt());
    uint8_t buffer[16];
    memset(buffer, 0x5A, sizeof(buffer));
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), BIG_LENGTH - 4, buffer, sizeof(buffer), false), 4);
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), BIG_LENGTH, buffer, sizeof(buffer), false), 0);
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), BIG_LENGTH - 4, buffer, sizeof(buffer), true), 4);
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), BIG_LENGTH, buffer, sizeof(buffer), true), -1);

    /* nothing spilled into the block after the partition */
    uint8_t after[4] = { 1, 1, 1, 1 };
    DMOD_TEST_EXPECT_EQ(transfer(DISK, BIG_FIRST * BLOCK + BIG_LENGTH, after, sizeof(after), false), 4);
    DMOD_TEST_EXPECT_EQ(after[0] | after[1] | after[2] | after[3], 0);
}

DMOD_TEST_STEP(partitions_block_ioctls_are_translated)
{
    DMOD_TEST_EXPECT_TRUE(plug_big_gpt());
    dmdrvi_block_info_t info;
    memset(&info, 0, sizeof(info));
    DMOD_TEST_EXPECT_EQ(node_ioctl(PART(1), DMDRVI_IOCTL_BLOCK_GET_INFO, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.block_count, BIG_COUNT);
    DMOD_TEST_EXPECT_EQ(info.logical_block_size, BLOCK);

    uint8_t data[4] = { 9, 9, 9, 9 };
    uint8_t back[4] = { 1, 1, 1, 1 };
    DMOD_TEST_EXPECT_EQ(transfer(PART(1), 0, data, sizeof(data), true), 4);
    dmdrvi_block_range_t outside = { .offset = (dmdrvi_offset_t)(BIG_LENGTH - BLOCK), .length = 2u * BLOCK };
    dmdrvi_block_range_t first   = { .offset = 0, .length = BLOCK };
    DMOD_TEST_EXPECT_EQ(node_ioctl(PART(1), DMDRVI_IOCTL_BLOCK_ERASE, &outside), -EINVAL);
    DMOD_TEST_EXPECT_EQ(node_ioctl(PART(1), DMDRVI_IOCTL_BLOCK_ERASE, &first), 0);
    DMOD_TEST_EXPECT_EQ(transfer(DISK, BIG_FIRST * BLOCK, back, sizeof(back), false), 4);
    DMOD_TEST_EXPECT_EQ(back[0] | back[1] | back[2] | back[3], 0);     /* erased on the device */

    DMOD_TEST_EXPECT_EQ(node_ioctl(PART(1), DMDEVFS_MOCKDRV_IOCTL_GET_STATS, &info), -ENOTTY);
}

/* ======================================================================
 *  Removal and libsystemd reporting
 * ====================================================================== */

static bool wait_state(const char* unit, dmosi_process_state_t state)
{
    for (int waited = 0; waited < WAIT_MS; waited += 10)
    {
        libsystemd_service_status_t status;
        if (libsystemd_status(unit, &status) == 0 && status.state == state)
        {
            return true;
        }
        dmosi_thread_sleep(10);
    }
    return false;
}

/* Running and set up (see testsvc/testsvc.c) - only then may it be stopped. */
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

DMOD_TEST_STEP(partitions_are_reported_and_withdrawn_with_their_device)
{
    DMOD_TEST_EXPECT_EQ(libsystemd_scan(DMDEVFS_TEST_FIXTURES_DIR "/units"), 0);
    DMOD_TEST_EXPECT_EQ(libsystemd_load_rules(DMDEVFS_TEST_FIXTURES_DIR "/rules"), 0);

    mbr_part_t parts[] = { { 0x0C, 2048, 1024 }, { 0x83, 4096, 1024 } };
    DMOD_TEST_EXPECT_TRUE(set_media(16384) && write_mbr(parts, 2) && plug());
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv0_0", "/dev" DISK));
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv0_0p1", "/dev" PART(1)));
    DMOD_TEST_EXPECT_TRUE(wait_ready("blk@dmdevfs_mockdrv0_0p2", "/dev" PART(2)));

    DMOD_TEST_EXPECT_EQ(node_ioctl(HOST, DMDEVFS_MOCKDRV_IOCTL_UNPLUG, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0p1", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0p2", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_TRUE(wait_state("blk@dmdevfs_mockdrv0_0", DMOSI_PROCESS_STATE_TERMINATED));
    DMOD_TEST_EXPECT_FALSE(exists(PART(1)));
    DMOD_TEST_EXPECT_FALSE(exists(PART(2)));
    DMOD_TEST_EXPECT_FALSE(exists(DISK));

    /* Rules off again: no other step starts services. */
    DMOD_TEST_EXPECT_EQ(libsystemd_load_rules(DMDEVFS_TEST_FIXTURES_DIR "/norules"), 0);
}
