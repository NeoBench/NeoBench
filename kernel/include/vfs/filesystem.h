#ifndef NEOBENCH_VFS_FILESYSTEM_H
#define NEOBENCH_VFS_FILESYSTEM_H

#include <stdint.h>
#include <sys/types.h>

typedef struct vfs_filesystem vfs_filesystem_t;

typedef int (*vfs_lookup_fn)(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
);

/*
 * Report the size in bytes of the regular file inode_number.
 */
typedef int (*vfs_get_size_fn)(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t *size
);

/*
 * Read inode_number's data at offset into buffer, up to size bytes.
 * Returns the number of bytes read, or -1 on error.
 */
typedef ssize_t (*vfs_read_fn)(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size
);

struct vfs_filesystem {
    const char *name;
    uint32_t block_size;
    uint64_t flags;
    void *private_data;

    vfs_lookup_fn lookup;
    vfs_get_size_fn get_size;
    vfs_read_fn read_file;
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
