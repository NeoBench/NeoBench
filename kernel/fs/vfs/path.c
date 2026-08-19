#include <stddef.h>
#include <stdint.h>

#include "vfs/path.h"
#include "vfs/filesystem.h"

int vfs_path_init(
    vfs_path_t *path,
    vfs_vnode_t *vnode
)
{
    if (!path || !vnode)
        return -1;

    path->vnode = vnode;

    vfs_vnode_get(vnode);

    return 0;
}

void vfs_path_destroy(
    vfs_path_t *path
)
{
    if (!path)
        return;

    if (path->vnode)
        vfs_vnode_put(path->vnode);

    path->vnode = 0;
}

int vfs_lookup(
    const vfs_path_t *parent,
    const char *name,
    vfs_vnode_t *result
)
{
    vfs_vnode_t *parent_vnode;
    vfs_filesystem_t *fs;

    uint64_t inode;
    uint32_t mode;
    vfs_vnode_type_t type;

    if (!parent || !parent->vnode)
        return -1;

    if (!name || !result)
        return -1;

    parent_vnode = parent->vnode;
    fs = parent_vnode->fs;

    if (!fs || !fs->lookup)
        return -1;

    if (parent_vnode->type != VFS_VNODE_DIR)
        return -1;

    if (fs->lookup(
            fs,
            parent_vnode->ino,
            name,
            &inode,
            &mode) != 0)
        return -1;

    if (inode == 0)
        return -1;

    if ((mode & 0xF000) == 0x4000)
        type = VFS_VNODE_DIR;
    else
        type = VFS_VNODE_REG;

    if (vfs_vnode_init(
            result,
            fs,
            inode,
            type) != 0)
        return -1;

    result->mode = mode;

    return 0;
}
