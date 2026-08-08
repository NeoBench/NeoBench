#ifndef NBFS_DIRECTORY_H
#define NBFS_DIRECTORY_H

#include <stdint.h>

#define NBFS_NAME_MAX 255

typedef struct
{
    uint32_t inode;

    uint16_t length;

    uint16_t type;

    char name[NBFS_NAME_MAX];

} nbfs_dirent_t;

#endif
