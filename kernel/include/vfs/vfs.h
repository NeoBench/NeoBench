#ifndef NEOBENCH_VFS_VFS_H
#define NEOBENCH_VFS_VFS_H

#include "vfs/filesystem.h"
#include "vfs/vnode.h"

/* Forward declaration from block/device.h */
struct block_device;

int vfs_init(void);
void vfs_shutdown(void);

int vfs_register_filesystem(vfs_filesystem_t *fs);
int vfs_unregister_filesystem(vfs_filesystem_t *fs);
vfs_filesystem_t *vfs_find_filesystem(const char *name);

/*
 * Mount an NBFS instance over the supplied block device as the
 * VFS root "/".
 */
int vfs_mount_root(
    const char *fsname,
    struct block_device *device
);

/*
 * The mounted root vnode, or NULL if not mounted.
 */
vfs_vnode_t *vfs_root(void);

/*
 * The filesystem instance mounted at "/", or NULL.
 */
vfs_filesystem_t *vfs_root_filesystem(void);

#endif
