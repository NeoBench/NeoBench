#ifndef NB_DIRECTORY_H
#define NB_DIRECTORY_H

#include <stdint.h>

int nbfs_lookup(
    const char *path,
    uint32_t *inode);

#endif
