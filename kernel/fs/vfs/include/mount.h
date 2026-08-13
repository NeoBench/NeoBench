#ifndef VFS_MOUNT_H
#define VFS_MOUNT_H

#include "filesystem.h"
#include "vnode.h"

typedef struct vfs_mount {
    vfs_filesystem_t *fs;
    vfs_vnode_t *root;
    int mounted;
} vfs_mount_t;

int vfs_mount_init(
    vfs_mount_t *mount,
    vfs_filesystem_t *fs,
    vfs_vnode_t *root
);

void vfs_mount_destroy(vfs_mount_t *mount);

#endif
