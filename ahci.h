#pragma once
#include "disk.h"
#include <stdint.h>

typedef struct {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
} hba_mem_t;

typedef struct {
    hba_mem_t* abar;
    uint32_t port;
} ahci_disk_t;
