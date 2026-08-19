#ifndef NEOLOADER_LOADER_H
#define NEOLOADER_LOADER_H

#include <stdint.h>

#define NEOLOADER_OK        0
#define NEOLOADER_BAD_ELF  -1
#define NEOLOADER_BAD_ARCH -2
#define NEOLOADER_BAD_LOAD -3

int neo_load_kernel(
    const void *image,
    uint32_t image_size
);

#endif
