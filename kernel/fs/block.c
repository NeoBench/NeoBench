#include "block/device.h"

int block_device_init(
    block_device_t *dev,
    const char *name,
    uint32_t block_size,
    uint64_t block_count
)
{
    if (!dev || !name || block_size == 0 || block_count == 0)
        return -1;

    dev->name = name;
    dev->block_size = block_size;
    dev->block_count = block_count;
    dev->private_data = 0;
    dev->read = 0;
    dev->write = 0;

    return 0;
}

int block_device_read(
    block_device_t *dev,
    uint64_t block,
    void *buffer
)
{
    if (!dev || !buffer || !dev->read)
        return -1;

    if (block >= dev->block_count)
        return -1;

    return dev->read(dev, block, buffer);
}

int block_device_write(
    block_device_t *dev,
    uint64_t block,
    const void *buffer
)
{
    if (!dev || !buffer || !dev->write)
        return -1;

    if (block >= dev->block_count)
        return -1;

    return dev->write(dev, block, buffer);
}

void block_device_destroy(block_device_t *dev)
{
    if (!dev)
        return;

    dev->name = 0;
    dev->block_size = 0;
    dev->block_count = 0;
    dev->private_data = 0;
    dev->read = 0;
    dev->write = 0;
}
