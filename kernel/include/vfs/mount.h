#ifndef NEOBENCH_VFS_MOUNT_H
#define NEOBENCH_VFS_MOUNT_H

#include <stdint.h>

#include "vfs/filesystem.h"
#include "vfs/vnode.h"

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

void vfs_mount_destroy(
    vfs_mount_t *mount
);

#endif
