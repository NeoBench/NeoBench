#include <stddef.h>
#include <string.h>

#include "block/memdisk.h"

static int memdisk_read(
    block_device_t *dev,
    uint64_t block,
    void *buffer)
{
    memdisk_t *disk;
    uint64_t byte_offset;

    if (!dev || !buffer)
        return -1;

    disk = (memdisk_t *)dev->private_data;

    if (!disk)
        return -1;

    byte_offset = block * dev->block_size;

    if (byte_offset + dev->block_size > disk->image_bytes)
        return -1;

    memcpy(
        buffer,
        (const uint8_t *)disk->image + byte_offset,
        dev->block_size);

    return 0;
}

static int memdisk_write(
    block_device_t *dev,
    uint64_t block,
    const void *buffer)
{
    memdisk_t *disk;
    uint64_t byte_offset;

    if (!dev || !buffer)
        return -1;

    disk = (memdisk_t *)dev->private_data;

    if (!disk)
        return -1;

    byte_offset = block * dev->block_size;

    if (byte_offset + dev->block_size > disk->image_bytes)
        return -1;

    memcpy(
        (uint8_t *)disk->image + byte_offset,
        buffer,
        dev->block_size);

    return 0;
}

int memdisk_attach(
    memdisk_t *disk,
    const char *name,
    const void *image,
    uint64_t image_bytes,
    uint32_t block_size)
{
    if (!disk || !name || !image || block_size == 0)
        return -1;

    if (image_bytes < block_size ||
        image_bytes % block_size != 0)
        return -1;

    disk->image = image;
    disk->image_bytes = image_bytes;

    if (block_device_init(
            &disk->dev,
            name,
            block_size,
            image_bytes / block_size) != 0)
        return -1;

    disk->dev.private_data = disk;
    disk->dev.read = memdisk_read;
    disk->dev.write = memdisk_write;

    return 0;
}

block_device_t *memdisk_device(memdisk_t *disk)
{
    if (!disk)
        return NULL;

    return &disk->dev;
}