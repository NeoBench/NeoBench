#include <stddef.h>
#include <stdint.h>

#include "vfs/file.h"

int vfs_file_init(
    vfs_file_t *file,
    vfs_vnode_t *vnode,
    uint32_t flags
)
{
    if (!file || !vnode)
        return -1;

    file->vnode = vnode;
    file->offset = 0;
    file->flags = flags;

    vfs_vnode_get(vnode);

    return 0;
}

void vfs_file_destroy(vfs_file_t *file)
{
    if (!file)
        return;

    if (file->vnode)
        vfs_vnode_put(file->vnode);

    file->vnode = 0;
    file->offset = 0;
    file->flags = 0;
}
