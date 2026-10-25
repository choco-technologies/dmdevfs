/**
 * @file main.c
 * @brief devscan - tell a dmdevfs mount that a file system came or went
 *
 *     devscan add    <dmdevfs path> <mount point>
 *     devscan remove <dmdevfs path> <mount point>
 *
 * A mounted file system - an SD card, the external flash - may bring driver configurations of its own, in
 * <mount point>/cfg/drivers (the layout of the directory dmdevfs is configured from). devscan opens the root of the
 * dmdevfs mount (<dmdevfs path>, e.g. /dev) and asks it, with an ioctl, to configure them (DMDEVFS_IOCTL_ADD_CONFIG_DIR)
 * or to drop them again (DMDEVFS_IOCTL_REMOVE_CONFIG_DIR). It is all dmdevfs-specific, so it lives here and not in
 * libsystemd.
 *
 * libsystemd reports every mount as a device of class "mount" (dmfsi's _mount_changed dif) and the rule of
 * configs/devscan.rules starts `devscan add` for it, and `devscan remove` when it is about to be unmounted (the
 * rule's "remove" key - the unmount waits for it). Both end when they are done: nothing stays running. The units and
 * rules that come with a mount are not its business.
 *
 * A mount point without cfg/drivers is left alone (dmdevfs finds nothing there).
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmdevfs.h"
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
    bool add = argc == 4 && strcmp(argv[1], "add") == 0;
    bool remove = argc == 4 && strcmp(argv[1], "remove") == 0;
    if (!add && !remove)
    {
        Dmod_Printf("Usage: devscan <add|remove> <dmdevfs path> <mount point>\n");
        Dmod_Printf("Configures/drops <mount point>/cfg/drivers in the dmdevfs mounted at <dmdevfs path>.\n");
        return -EINVAL;
    }
    const char* devfs_path = argv[2];
    const char* mount_point = argv[3];

    void* devfs = Dmod_FileOpen(devfs_path, "r");
    if (devfs == NULL)
    {
        DMOD_LOG_WARN("devscan: cannot open %s - is dmdevfs mounted there?\n", devfs_path);
        return -ENODEV;
    }
    int ret = Dmod_Ioctl(devfs, add ? DMDEVFS_IOCTL_ADD_CONFIG_DIR : DMDEVFS_IOCTL_REMOVE_CONFIG_DIR, (void*)mount_point);
    Dmod_FileClose(devfs);
    if (ret != 0)
    {
        DMOD_LOG_WARN("devscan: %s of %s in %s failed (%d)\n", add ? "adding" : "removing", mount_point, devfs_path, ret);
    }
    return ret;
}
