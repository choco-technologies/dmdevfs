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

/** Runtime configuration-tree management. Pass a NUL-terminated directory
 * path as arg to ioctl on any node of the target mount (prefer /dev/null).
 * Calls are synchronous and run in FIFO order with hot-plug work. Do not
 * call synchronously from a driver/service callback invoked by that worker.
 * Paths are matched lexically, with repeated slashes and '.' normalized;
 * '..' components are rejected. Use absolute paths in applications.
 * ADD rejects duplicate/overlapping active trees and rolls back new nodes
 * on failure. REMOVE needs no access to the source files, withdraws only
 * its nodes and invalidates their handles (close remains valid). Driver
 * contexts are reclaimed after the last file/directory reference is released. Returns 0 or a
 * negative errno (EEXIST, EINVAL, ENOENT, EIO, ENOMEM, ENODEV, EDEADLK).
 */
#define DMDEVFS_IOCTL_ADD_CONFIG     0xF01
#define DMDEVFS_IOCTL_REMOVE_CONFIG  0xF02

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

#ifdef __cplusplus
}
#endif

#endif // DMDEVFS_H
