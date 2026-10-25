# mountcfg

Loads the configuration directory of every mount point. A mounted file system -
an SD card, the external flash - may bring its own configuration, in the
layout of the firmware's `/cfg`:

```
<mount point>/cfg/drivers/    driver configurations   -> dmdevfs
<mount point>/cfg/services/   units                   -> libsystemd
<mount point>/cfg/rules/      device rules            -> libsystemd
```

mountcfg implements dmvfs's `_mount_changed` DIF (`dmvfs_dif.h`) - dmvfs
calls it for every mount and unmount - and hands the directory to dmdevfs
(`dmdevfs_add_config_dir()`, see `include/dmdevfs_cfg.h`) and libsystemd
(`libsystemd_add_config_dir()`):

- **mounted** - the drivers are configured (on dmdevfs's hot-plug thread) and
  the units and rules are added to libsystemd's and started; a mount point
  without `cfg/` is left alone;
- **about to be unmounted** - its units stop (while their files are still
  there) and go away, then its drivers are removed.

The work is done on mountcfg's own thread (4 kB of stack - starting a unit
loads its module from the card), not in the DIF call, which dmvfs makes in
whatever thread mounts: a mount is queued and not waited for, an unmount waits
(up to 30 s) until the units of that file system have stopped.

The mounts there are when mountcfg starts are loaded at once (read with
`dmvfs_get_mount_point()`). The firmware's
own `/cfg` - the configuration directory of the root file system - is loaded
once, by dmdevfs's mount and libsystemd's scan, as before.

A unit of such a directory names the files of its own file system with `%m`
(the mount point), so it stays bound to that card even when several cards
carry the same module:

```ini
; <card>/cfg/services/carhmi/carhmi.ini
description=Car HMI demo
exec=%m/bin/carhmi.dmfc
args=%m/views/carhmi_view/carhmi.dmv
```

## Use

mountcfg is a Library module, started by its `type=library` unit
[`configs/mountcfg.ini`](configs/mountcfg.ini). With dmod-boot, one line in a
`*.dmd` file of the firmware:

```
mountcfg service=mountcfg.ini
```

dmdevfs and libsystemd do not depend on dmvfs - they are tested on the host
loader, which has none. mountcfg does, so it runs on the target only.
