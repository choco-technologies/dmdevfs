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
 *    mouse, buttons).
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

#ifdef __cplusplus
}
#endif

#endif // DMDEVFS_H
