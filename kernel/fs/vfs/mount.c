#include "vfs/mount.h"

int vfs_mount_init(
    vfs_mount_t *mount,
    vfs_filesystem_t *fs,
    vfs_vnode_t *root
)
{
    if (!mount || !fs || !root)
        return -1;

    mount->fs = fs;
    mount->root = root;
    mount->mounted = 1;

    return 0;
}

void vfs_mount_destroy(vfs_mount_t *mount)
{
    if (!mount)
        return;

    mount->fs = 0;
    mount->root = 0;
    mount->mounted = 0;
}
