/**
 * @file main.c
 * @brief mountcfg - load or unload the configuration directory of a mount point
 *
 *     mountcfg load   <mount point>
 *     mountcfg unload <mount point>
 *
 * A mounted file system - an SD card, the external flash - may bring its own configuration, in the layout of the
 * firmware's /cfg:
 *
 *     <mount point>/cfg/drivers    driver configurations  -> dmdevfs
 *     <mount point>/cfg/services   units                  -> libsystemd
 *     <mount point>/cfg/rules      device rules           -> libsystemd
 *
 * libsystemd reports every mount as a device of class "mount" (dmfsi's _mount_changed dif) and the rule of
 * configs/mountcfg.rules starts `mountcfg load` for it, and `mountcfg unload` when it is about to be unmounted
 * (the rule's "remove" key - the unmount waits for it). Both end when they are done: nothing stays running.
 *
 * load:   the drivers first (dmdevfs configures them on its own thread), then the units and rules.
 * unload: the units first, while their files are still there, then the drivers.
 *
 * A mount point without cfg/ is left alone.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "libsystemd.h"
#include "dmdevfs_cfg.h"
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
    bool load = argc == 3 && strcmp(argv[1], "load") == 0;
    bool unload = argc == 3 && strcmp(argv[1], "unload") == 0;
    if (!load && !unload)
    {
        Dmod_Printf("Usage: mountcfg <load|unload> <mount point>\n");
        Dmod_Printf("Loads/unloads <mount point>/cfg into dmdevfs (drivers) and libsystemd (services, rules).\n");
        return -EINVAL;
    }
    const char* mount_point = argv[2];

    if (load)
    {
        int ret = dmdevfs_add_config_dir(mount_point);
        if (ret != 0)
        {
            DMOD_LOG_WARN("mountcfg: cannot load the drivers of %s (%d)\n", mount_point, ret);
        }
        ret = libsystemd_add_config_dir(mount_point);
        if (ret != 0 && ret != -ENOENT && ret != -EALREADY)      // -ENOENT: there is no cfg/ there
        {
            DMOD_LOG_WARN("mountcfg: cannot load the services of %s (%d)\n", mount_point, ret);
            return ret;
        }
        return 0;
    }

    int ret = libsystemd_remove_config_dir(mount_point);
    dmdevfs_remove_config_dir(mount_point);
    return (ret == 0 || ret == -ENOENT) ? 0 : ret;
}
