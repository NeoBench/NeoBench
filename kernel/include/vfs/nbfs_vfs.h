#ifndef NBFS_VFS_H
#define NBFS_VFS_H

#include <stdint.h>
#include <sys/types.h>

#include "vfs/filesystem.h"
#include "libnbfs.h"

/*
 * Bind an open libnbfs context to a VFS filesystem, installing the
 * userspace NBFS operations (lookup, get_size, read_file).
 */
int vfs_nbfs_bind(
    vfs_filesystem_t *fs,
    nbfs_context_t *ctx
);

int vfs_nbfs_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
);

int vfs_nbfs_get_size(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t *size
);

ssize_t vfs_nbfs_read(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size
);

#endif
