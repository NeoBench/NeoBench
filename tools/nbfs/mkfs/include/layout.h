#ifndef NBFS_LAYOUT_H
#define NBFS_LAYOUT_H

#include <nbfs/nbfs.h>

/*
 * mkfs internal layout aliases.
 *
 * Keep these names separate from the public NBFS constants so there
 * is no recursive macro expansion.
 */

#define NBFS_BOOT_BLOCK_POS         NBFS_BOOT_BLOCK
#define NBFS_SUPERBLOCK_BLOCK       NBFS_SUPERBLOCK
#define NBFS_BLOCK_BITMAP_BLOCK     NBFS_BLOCK_BITMAP
#define NBFS_INODE_BITMAP_BLOCK     NBFS_INODE_BITMAP
#define NBFS_INODE_TABLE_BLOCK      NBFS_INODE_TABLE
#define NBFS_INODE_TABLE_BLOCKS_V1  NBFS_INODE_TABLE_BLOCKS

#define NBFS_JOURNAL_START_BLOCK    NBFS_JOURNAL_START
#define NBFS_JOURNAL_BLOCK_COUNT    NBFS_JOURNAL_BLOCKS

#define NBFS_DATA_BLOCK             NBFS_DATA_START

#endif /* NBFS_LAYOUT_H */
