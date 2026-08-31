#include "vfs/vnode.h"

int vfs_vnode_init(
    vfs_vnode_t *vnode,
    struct vfs_filesystem *fs,
    vfs_ino_t ino,
    vfs_vnode_type_t type
)
{
    if (!vnode || !fs || ino == 0)
        return -1;

    vnode->ino = ino;
    vnode->type = type;
    vnode->mode = 0;
    vnode->refcount = 1;
    vnode->fs = fs;

    return 0;
}

void vfs_vnode_get(vfs_vnode_t *vnode)
{
    if (vnode)
        ++vnode->refcount;
}

void vfs_vnode_put(vfs_vnode_t *vnode)
{
    if (!vnode || vnode->refcount == 0)
        return;

    --vnode->refcount;
}
