#ifndef NEO_BENCH_NBFS_H
#define NEO_BENCH_NBFS_H

#include <stdint.h>

#include "block/device.h"
#include "vfs/filesystem.h"

typedef struct nbfs_mount {
    block_device_t *device;
    uint32_t block_size;
    uint64_t block_count;
    uint64_t root_inode;
    int mounted;
} nbfs_mount_t;

int nbfs_kernel_init(void);

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device
);

int nbfs_kernel_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    unsigned long *result_type
);

void nbfs_kernel_shutdown(void);

#endif
