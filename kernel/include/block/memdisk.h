#ifndef NEOBENCH_BLOCK_MEMDISK_H
#define NEOBENCH_BLOCK_MEMDISK_H

#include <stdint.h>

#include "block/device.h"

/*
 * Memory backed block device capable of carrying a complete
 * NBFS image.  Used to prove the filesystem/VFS stack without
 * physical Amiga storage hardware.
 *
 * The image is copied out block-at-a-time (byte-swapped by the
 * NBFS layer when needed), so the source buffer may reside in
 * readonly kernel .data.
 *
 * The struct carries its own block_device_t so no heap is needed.
 */
typedef struct
{
    const void *image;
    uint64_t image_bytes;
    block_device_t dev;
} memdisk_t;

int memdisk_attach(
    memdisk_t *disk,
    const char *name,
    const void *image,
    uint64_t image_bytes,
    uint32_t block_size
);

block_device_t *memdisk_device(memdisk_t *disk);

#endif