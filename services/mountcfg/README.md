# mountcfg

A one-shot service that loads the configuration directory of a mount point. A
mounted file system - an SD card, the external flash - may bring its own
configuration, in the layout of the firmware's `/cfg`:

```
<mount point>/cfg/drivers/    driver configurations   -> dmdevfs
<mount point>/cfg/services/   units                   -> libsystemd
<mount point>/cfg/rules/      device rules            -> libsystemd
```

```
mountcfg load   <mount point>   # drivers first, then the units and rules
mountcfg unload <mount point>   # the units first (their files are still there), then the drivers
```

It calls `dmdevfs_add_config_dir()` / `dmdevfs_remove_config_dir()`
(`include/dmdevfs_cfg.h`) and `libsystemd_add_config_dir()` /
`libsystemd_remove_config_dir()`, and ends. A mount point without `cfg/` is left
alone.

## How it is started

Nothing here follows mounts. libsystemd reports every mount as a device of class
`mount` (it implements dmfsi's `_mount_changed` dif, which dmvfs calls), named
after the mount point (`/mnt/SD` -> `mnt_SD`) with the mount point as its value,
and [`configs/mountcfg.rules`](configs/mountcfg.rules) says what to do:

```ini
[class=mount]
start=mountcfg@%name              # mounted: load its cfg/
remove=mountcfg-unload@%name      # about to be unmounted: unload it
```

[`mountcfg@.ini`](configs/mountcfg@.ini) and
[`mountcfg-unload@.ini`](configs/mountcfg-unload@.ini) are `type=oneshot` units
running `mountcfg load %v` / `mountcfg unload %v`. libsystemd waits for the
`remove` unit before the unmount goes on, so the units from the file system
stop while their files are still there. Both processes have the stack of this
module (4 kB) - the thread that mounts (automount's) has far less - and none
stays running afterwards.

With dmod-boot, in a `*.dmd` file of the firmware:

```
mountcfg service=mountcfg@.ini
mountcfg service=mountcfg-unload@.ini
mountcfg rules=mountcfg.rules
```

In a unit of such a directory `%m` is the mount point (`exec=%m/bin/app.dmfc`),
so it stays bound to its own card.
