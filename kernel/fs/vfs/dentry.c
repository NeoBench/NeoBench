#include <string.h>

#include "include/dentry.h"

int vfs_dentry_init(
    vfs_dentry_t *dentry,
    const char *name,
    vfs_vnode_t *parent,
    vfs_vnode_t *vnode
)
{
    size_t len;

    if (!dentry || !name || !parent || !vnode)
        return -1;

    len = strlen(name);

    if (len == 0 || len > VFS_DENTRY_NAME_MAX)
        return -1;

    memcpy(dentry->name, name, len + 1);

    dentry->parent = parent;
    dentry->vnode = vnode;
    dentry->refcount = 1;

    vfs_vnode_get(parent);
    vfs_vnode_get(vnode);

    return 0;
}

void vfs_dentry_get(vfs_dentry_t *dentry)
{
    if (dentry)
        ++dentry->refcount;
}

void vfs_dentry_put(vfs_dentry_t *dentry)
{
    if (!dentry || dentry->refcount == 0)
        return;

    --dentry->refcount;

    if (dentry->refcount == 0) {
        vfs_vnode_put(dentry->parent);
        vfs_vnode_put(dentry->vnode);
    }
}
