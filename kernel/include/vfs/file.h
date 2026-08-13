#ifndef NEOBENCH_VFS_FILE_H
#define NEOBENCH_VFS_FILE_H

#include <stddef.h>
#include <stdint.h>

#include "vfs/vnode.h"

typedef struct vfs_file {
    vfs_vnode_t *vnode;
    uint64_t offset;
    uint32_t flags;
} vfs_file_t;

int vfs_file_init(
    vfs_file_t *file,
    vfs_vnode_t *vnode,
    uint32_t flags
);

void vfs_file_destroy(
    vfs_file_t *file
);

#endif
