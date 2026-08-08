#ifndef NB_BOOTINFO_H
#define NB_BOOTINFO_H

#include <stdint.h>

#define NB_BOOT_MAGIC   0x4E424F54U /* "NBOT" */
#define NB_BOOT_VERSION 1

typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t ram_size;
    uint32_t kernel_base;
    uint32_t kernel_size;

    uint32_t boot_device;

    uint32_t framebuffer;

    uint32_t cmdline;

} nb_bootinfo_t;

#endif
