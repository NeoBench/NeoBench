#ifndef VFS_DENTRY_H
#define VFS_DENTRY_H

#include <stddef.h>

#include "vnode.h"

#define VFS_DENTRY_NAME_MAX 255

typedef struct vfs_dentry {
    char name[VFS_DENTRY_NAME_MAX + 1];

    vfs_vnode_t *parent;
    vfs_vnode_t *vnode;

    uint32_t refcount;
} vfs_dentry_t;

int vfs_dentry_init(
    vfs_dentry_t *dentry,
    const char *name,
    vfs_vnode_t *parent,
    vfs_vnode_t *vnode
);

void vfs_dentry_get(vfs_dentry_t *dentry);
void vfs_dentry_put(vfs_dentry_t *dentry);

#endif
