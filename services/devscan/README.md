# devscan

A one-shot service that tells a dmdevfs mount a file system came or went. A
mounted file system - an SD card, the external flash - may bring driver
configurations of its own, in `<mount point>/cfg/drivers` (the layout of the
directory dmdevfs is configured from):

```
devscan add    <dmdevfs path> <mount point>   # configure <mount point>/cfg/drivers
devscan remove <dmdevfs path> <mount point>   # drop them again
```

It opens the root of the dmdevfs mount (`<dmdevfs path>`, `/dev` on dmod-boot)
and does an ioctl on it - `DMDEVFS_IOCTL_ADD_CONFIG_DIR` /
`DMDEVFS_IOCTL_REMOVE_CONFIG_DIR` with the mount point as argument, see
[`include/dmdevfs.h`](../../include/dmdevfs.h) - then ends. dmdevfs queues the
work for its hot-plug thread. A mount point without `cfg/drivers` is left alone.

## How it is started

Nothing here follows mounts. libsystemd reports every mount as a device of class
`mount` (it implements dmfsi's `_mount_changed` dif, which dmvfs calls), named
after the mount point (`/mnt/SD` -> `mnt_SD`) with the mount point as its value,
and [`configs/devscan.rules`](configs/devscan.rules) says what to do:

```ini
[class=mount]
start=devscan@%name              # mounted: configure its cfg/drivers
remove=devscan-remove@%name      # about to be unmounted: drop them
```

[`devscan@.ini`](configs/devscan@.ini) and
[`devscan-remove@.ini`](configs/devscan-remove@.ini) are `type=oneshot` units
running `devscan add /dev %v` / `devscan remove /dev %v`. libsystemd waits for
the `remove` unit before the unmount goes on, so the nodes of the file system
are gone - and the services started for them stopped - while it is still there.
Both processes have the stack of this module (2 kB) - the thread that mounts
(automount's) has far less - and none stays running afterwards.

The path of the dmdevfs is in the unit file (`/dev`, where dmod-boot mounts
it); a system with another one, or a second one, gives its units their own path.

With dmod-boot, in a `*.dmd` file of the firmware:

```
devscan service=devscan@.ini
devscan service=devscan-remove@.ini
devscan rules=devscan.rules
```
