#pragma once
#include <stdint.h>

#define SECTOR_SIZE 512

typedef struct disk_t disk_t;

typedef struct {
    uint32_t sector_size;
    uint64_t total_sectors;

    int (*read)(disk_t*, uint64_t lba, void* buf, uint32_t count);
    int (*write)(disk_t*, uint64_t lba, const void* buf, uint32_t count);
    int (*flush)(disk_t*);
} disk_ops_t;

struct disk_t {
    disk_ops_t* ops;
    void* driver_data;
};
