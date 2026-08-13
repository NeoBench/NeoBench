#ifndef VFS_FILESYSTEM_H
#define VFS_FILESYSTEM_H

#include <stdint.h>

struct vfs_mount;

typedef struct vfs_filesystem {
    const char *name;

    uint32_t block_size;
    uint64_t root_ino;

    struct vfs_mount *mount;
    void *private_data;
} vfs_filesystem_t;

int vfs_filesystem_init(
    vfs_filesystem_t *fs,
    const char *name,
    uint32_t block_size,
    uint64_t root_ino
);

void vfs_filesystem_destroy(vfs_filesystem_t *fs);

#endif
