#ifndef NBFS_VFS_H
#define NBFS_VFS_H

#include <stdint.h>

#include "vfs/filesystem.h"

int vfs_nbfs_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
);

#endif
