#include <stdint.h>
#include <nbfs/inode.h>
#include "disk.h"
#include <nbfs/layout.h>

#define INODES_PER_BLOCK \
    (NBFS_BLOCK_SIZE / sizeof(nbfs_inode_t))

int nbfs_read_inode(
    uint32_t inode_number,
    nbfs_inode_t *inode)
{
    uint32_t block =
        NBFS_INODE_TABLE_BLOCK +
        (inode_number / INODES_PER_BLOCK);

    uint8_t buffer[NBFS_BLOCK_SIZE];

    if (!disk_read_blocks(block, 1, buffer))
        return 0;

    nbfs_inode_t *table =
        (nbfs_inode_t *)buffer;

    *inode =
        table[inode_number % INODES_PER_BLOCK];

    return 1;
}
