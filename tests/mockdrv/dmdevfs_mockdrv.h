#ifndef DMDEVFS_MOCKDRV_H
#define DMDEVFS_MOCKDRV_H

/*
 * Control ioctls of the test driver's host node (/dmdevfs_mockdrv<major>).
 * The test goes through dmdevfs for them, so the driver is loaded by dmdevfs
 * itself and its dmdrvi_device_available()/_unavailable() calls reach it.
 */
#define DMDEVFS_MOCKDRV_IOCTL_PLUG      0x1000  /* arg: NULL - announce block child /dmdevfs_mockdrv<major>/0 */
#define DMDEVFS_MOCKDRV_IOCTL_UNPLUG    0x1001  /* arg: NULL - withdraw it */
#define DMDEVFS_MOCKDRV_IOCTL_GET_STATS 0x1002  /* arg: dmdevfs_mockdrv_stats_t* */
#define DMDEVFS_MOCKDRV_IOCTL_SET_MEDIA 0x1003  /* arg: const uint64_t* block count - only while unplugged; clears the medium */
#define DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR 0x1004 /* arg: const dmdevfs_mockdrv_sector_t* - raw write, plugged or not */

/** Logical block size of the child's medium. */
#define DMDEVFS_MOCKDRV_BLOCK_SIZE      512u

#include <stdint.h>

/** Monitor contract calls a monitored host node received so far. */
typedef struct
{
    uint32_t    events;     /**< DMDRVI_IOCTL_MONITOR_EVENT */
    uint32_t    refreshes;  /**< DMDRVI_IOCTL_MONITOR_REFRESH */
} dmdevfs_mockdrv_stats_t;

/** One raw sector for DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR. */
typedef struct
{
    uint64_t    lba;
    uint8_t     data[DMDEVFS_MOCKDRV_BLOCK_SIZE];
} dmdevfs_mockdrv_sector_t;

#endif // DMDEVFS_MOCKDRV_H
