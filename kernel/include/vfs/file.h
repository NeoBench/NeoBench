#ifndef NEOBENCH_VFS_FILE_H
#define NEOBENCH_VFS_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "vfs/vnode.h"
#include "vfs/path.h"

typedef struct vfs_file {
    vfs_vnode_t *vnode;
    vfs_vnode_t vnode_storage;
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

ssize_t vfs_file_read(
    vfs_file_t *file,
    void *buffer,
    size_t size
);

int vfs_open(
    const vfs_path_t *root,
    const char *path,
    uint32_t flags,
    vfs_file_t *file
);

#endif
