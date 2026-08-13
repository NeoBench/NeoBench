#ifndef NEOBENCH_NBFS_MOUNT_H
#define NEOBENCH_NBFS_MOUNT_H

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

#endif
