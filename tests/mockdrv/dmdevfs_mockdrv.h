#ifndef DMDEVFS_MOCKDRV_H
#define DMDEVFS_MOCKDRV_H

/*
 * Control ioctls of the test driver's host node (/dmdevfs_mockdrv<major>).
 * The test goes through dmdevfs for them, so the driver is loaded by dmdevfs
 * itself and its dmdrvi_device_available()/_unavailable() calls reach it.
 */
#define DMDEVFS_MOCKDRV_IOCTL_PLUG      0x1000  /* arg: NULL - announce block child /dmdevfs_mockdrv<major>/0 */
#define DMDEVFS_MOCKDRV_IOCTL_UNPLUG    0x1001  /* arg: NULL - withdraw it */

#endif // DMDEVFS_MOCKDRV_H
