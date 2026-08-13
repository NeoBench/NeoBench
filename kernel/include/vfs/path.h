#ifndef NEOBENCH_VFS_PATH_H
#define NEOBENCH_VFS_PATH_H

#include "vnode.h"
struct vfs_filesystem;

typedef struct vfs_path {
    vfs_vnode_t *vnode;
    struct vfs_filesystem *fs;
} vfs_path_t;

int vfs_path_init(
    vfs_path_t *path,
    vfs_vnode_t *vnode
);

void vfs_path_destroy(
    vfs_path_t *path
);

int vfs_lookup(
    const vfs_path_t *parent,
    const char *name,
    vfs_vnode_t *result
);

#endif
