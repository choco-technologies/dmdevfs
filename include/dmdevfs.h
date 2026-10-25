/**
 * @file dmdevfs.h
 * @brief DMOD Driver File System - Public header
 * @author Patryk Kubiak
 */

#ifndef DMDEVFS_H
#define DMDEVFS_H

#ifdef __cplusplus
extern "C" {
#endif

// Module version
#define DMDEVFS_VERSION_MAJOR 0
#define DMDEVFS_VERSION_MINOR 2

/**
 * @brief Device classes dmdevfs reports nodes under to libsystemd
 *
 * Once a node's absolute path is known, dmdevfs asks its driver what the
 * node is and reports it under every class it answers to, so device rules
 * (`[class=<name>]`) can start services for it:
 *
 *  - "monitor": DMDRVI_IOCTL_MONITOR_GET_POLICY - a monitor service
 *    (dmdevmon) has to drive it;
 *  - "block":   DMDRVI_IOCTL_BLOCK_GET_INFO - a block device, e.g. for
 *    automount;
 *  - "display": DMDRVI_IOCTL_GFX_GET_INFO - a display, e.g. for dmview;
 *  - "input":   DMDRVI_IOCTL_INPUT_GET_INFO - an input device (touch panel,
 *    mouse, buttons);
 *  - "audio":   DMDRVI_IOCTL_AUDIO_GET_INFO - an audio codec or other audio
 *    device, e.g. for a player or mixer service;
 *  - "rtc": DMDRVI_IOCTL_RTC_GET_INFO - an RTC, including unset calendars,
 *    e.g. for the dmutc library service.
 *
 * The device name is the node path relative to the mount with '/' replaced
 * by '_' (unit names cannot contain '/'): "/dmsdio0/0" -> "dmsdio0_0". The
 * user value is the node's absolute path. Removal is reported when the node
 * goes away, which makes libsystemd stop the matching units. These are the
 * values of the `report=` configuration key, too (plus "all" and "none").
 *
 * The members of a node's friends_group are available to every module
 * through DMDRVI_IOCTL_DEVFS_GET_FRIEND on the node (dmdrvi_ioctl.h).
 */
#define DMDEVFS_CLASS_MONITOR   "monitor"
#define DMDEVFS_CLASS_BLOCK     "block"
#define DMDEVFS_CLASS_DISPLAY   "display"
#define DMDEVFS_CLASS_INPUT     "input"
#define DMDEVFS_CLASS_AUDIO     "audio"
#define DMDEVFS_CLASS_RTC       "rtc"

/**
 * @brief Commands of the mount itself: ioctl on its root
 *
 * Opening the root of a dmdevfs mount (`/dev` itself, not a node in it) gives a
 * handle without a device behind it - reads report EOF, writes are discarded -
 * that answers these commands. The command works on the mount that was opened,
 * and only on it.
 *
 *  - DMDEVFS_IOCTL_ADD_CONFIG_DIR: every mounted file system may bring driver
 *    configurations of its own, in `<mount point>/cfg/drivers` - the same
 *    layout as the directory the dmdevfs mount is configured from. The argument
 *    is the mount point (a NUL-terminated string, e.g. "/mnt/SD"); its drivers
 *    are configured in this mount, exactly as if they were in its own directory.
 *    The directory the mount is configured from is never loaded twice.
 *  - DMDEVFS_IOCTL_REMOVE_CONFIG_DIR: removes them again - their nodes (with
 *    their partitions and hot-plugged devices) are reported removed, so the
 *    services started for them stop, and every driver is freed once the last
 *    handle open on its nodes is closed.
 *
 * Both only queue the work for dmdevfs's hot-plug thread, which also handles
 * hot-plug events, and return 0 at once (or a negative errno: -EINVAL for a
 * missing or empty argument, -ENODEV when dmdevfs is not initialized, -ENOMEM).
 * The devscan service (services/devscan) is what calls them, started by a
 * libsystemd rule for every mount and unmount.
 */
#define DMDEVFS_IOCTL_ADD_CONFIG_DIR        0xF10
#define DMDEVFS_IOCTL_REMOVE_CONFIG_DIR     0xF11

#ifdef __cplusplus
}
#endif

#endif // DMDEVFS_H
