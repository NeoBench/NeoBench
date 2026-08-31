#ifndef NEOBENCH_VFS_VNODE_H
#define NEOBENCH_VFS_VNODE_H

#include <stdint.h>

typedef uint64_t vfs_ino_t;

struct vfs_filesystem;

typedef enum {
    VFS_VNODE_REG = 1,
    VFS_VNODE_DIR = 2
} vfs_vnode_type_t;

typedef struct vfs_vnode {
    vfs_ino_t ino;
    vfs_vnode_type_t type;
    uint32_t mode;
    uint32_t refcount;

    struct vfs_filesystem *fs;
} vfs_vnode_t;

int vfs_vnode_init(
    vfs_vnode_t *vnode,
    struct vfs_filesystem *fs,
    vfs_ino_t ino,
    vfs_vnode_type_t type
);

void vfs_vnode_get(vfs_vnode_t *vnode);
void vfs_vnode_put(vfs_vnode_t *vnode);

#endif
