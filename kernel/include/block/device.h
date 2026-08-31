#ifndef NEOBENCH_BLOCK_DEVICE_H
#define NEOBENCH_BLOCK_DEVICE_H

#include <stdint.h>

typedef struct block_device block_device_t;

typedef int (*block_read_fn)(
    block_device_t *dev,
    uint64_t block,
    void *buffer
);

typedef int (*block_write_fn)(
    block_device_t *dev,
    uint64_t block,
    const void *buffer
);

struct block_device {
    const char *name;

    uint32_t block_size;
    uint64_t block_count;

    void *private_data;

    block_read_fn read;
    block_write_fn write;
};

int block_device_init(
    block_device_t *dev,
    const char *name,
    uint32_t block_size,
    uint64_t block_count
);

int block_device_read(
    block_device_t *dev,
    uint64_t block,
    void *buffer
);

int block_device_write(
    block_device_t *dev,
    uint64_t block,
    const void *buffer
);

void block_device_destroy(
    block_device_t *dev
);

#endif
