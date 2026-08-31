#ifndef NEOBENCH_BOOTINFO_H
#define NEOBENCH_BOOTINFO_H

#include <stdint.h>

#define NB_BOOTINFO_MAGIC 0x4E42494Eu /* 'NBIN' */
#define NB_BOOTINFO_VERSION 1
#define NB_MAX_MEM_RANGES 16

struct nb_mem_range {
    uint32_t base;
    uint32_t size;
    uint32_t flags;
};

struct nb_bootinfo {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;
    uint32_t kernel_base;
    uint32_t kernel_end;
    uint32_t framebuffer_base;
    uint32_t framebuffer_size;
    uint32_t framebuffer_pitch;
    uint16_t framebuffer_width;
    uint16_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  cpu_model;
    uint16_t reserved;
    uint32_t mem_count;
    struct nb_mem_range mem[NB_MAX_MEM_RANGES];
};

#endif
