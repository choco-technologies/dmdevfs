/**
 * @file dmdevfs_cfg.h
 * @brief DMOD Driver File System - configuration directories of mount points
 * @author Patryk Kubiak
 *
 * Kept apart from dmdevfs.h: including this header makes a module require
 * dmdevfs, the constants of dmdevfs.h do not.
 */

#ifndef DMDEVFS_CFG_H
#define DMDEVFS_CFG_H

#include "dmod.h"
#include "dmdevfs_defs.h"

/**
 * Every mounted file system may bring driver configurations of its own, in
 * `<mount point>/cfg/drivers` - the same layout as the directory a dmdevfs
 * mount is configured from (`/cfg/drivers` on dmod-boot). The mountcfg service
 * (services/mountcfg), started by a libsystemd rule for every mount and
 * unmount, calls these.
 *
 * dmdevfs_add_config_dir() configures them in every dmdevfs mount, exactly as
 * if they were in its own directory; dmdevfs_remove_config_dir() removes them
 * again - their nodes (with their partitions and hot-plugged devices) are
 * reported removed, so the services started for them stop, and every driver
 * is freed once the last handle open on its nodes is closed. The directory of
 * the mount's own configuration is never loaded twice.
 *
 * Both only queue the work for dmdevfs's hot-plug thread, which also handles
 * hot-plug events, and return at once.
 *
 * @retval 0        Queued
 * @retval -EINVAL  mount_point is NULL
 * @retval -ENODEV  dmdevfs is not initialized
 * @retval -ENOMEM  Out of memory
 */
dmod_dmdevfs_api(1.0, int, _add_config_dir, (const char* mount_point));
dmod_dmdevfs_api(1.0, int, _remove_config_dir, (const char* mount_point));

#endif // DMDEVFS_CFG_H
