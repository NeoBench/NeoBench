#pragma once
#include <cstdint>
#include <cstring>

enum {
    NBFS_KERNEL = 1,
    NBFS_SCRIPT = 3
};

struct nbfs_entry {
    char name[32];
    uint32_t inode;
    uint8_t type;
};

struct nbfs_index {
    uint32_t magic;
    uint32_t count;
    nbfs_entry entries[16];
};
