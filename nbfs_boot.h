#pragma once
#include <stdint.h>

#define NBFS_MAX_ENTRIES 32

enum {
    NBFS_KERNEL = 1,
    NBFS_MODULE = 2,
    NBFS_SCRIPT = 3
};

typedef struct {
    char name[32];        // "kernel.main"
    uint32_t inode;       // NeoFS inode
    uint8_t type;         // kernel/module/script
} nbfs_entry_t;

typedef struct {
    uint32_t magic;      // 'NBIX'
    uint32_t count;
    nbfs_entry_t entries[NBFS_MAX_ENTRIES];
} nbfs_index_t;
