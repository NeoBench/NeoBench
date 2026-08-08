#include <nbfs/nbfs.h>
#include "disk.h"

static nbfs_superblock_t g_superblock;

const nbfs_superblock_t *nbfs_superblock(void)
{
    return &g_superblock;
}

int nbfs_mount(void)
{
    if (!disk_read_blocks(0, 1, &g_superblock))
        return 0;

    if (g_superblock.version_major != NBFS_VERSION_MAJOR ||
        g_superblock.version_minor != NBFS_VERSION_MINOR)
    {
        return -1;
    }

    return 1;
}
