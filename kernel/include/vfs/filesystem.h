#ifndef NEOBENCH_VFS_FILESYSTEM_H
#define NEOBENCH_VFS_FILESYSTEM_H

#include <stdint.h>

typedef struct vfs_filesystem vfs_filesystem_t;

typedef int (*vfs_lookup_fn)(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
);

struct vfs_filesystem {
    const char *name;
    uint32_t block_size;
    uint64_t flags;
    void *private_data;

    vfs_lookup_fn lookup;
};

int vfs_filesystem_init(
    vfs_filesystem_t *fs,
    const char *name,
    uint32_t block_size,
    uint64_t flags
);

void vfs_filesystem_destroy(
    vfs_filesystem_t *fs
);

#endif
