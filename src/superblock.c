/*
 * NeoBench mkfs
 * superblock.c
 */

#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

int nbfs_write_superblock(FILE *fp)
{
    nbfs_superblock_t sb;

    memset(&sb,0,sizeof(sb));

    sb.magic            = NBFS_MAGIC;
    sb.version_major    = NBFS_VERSION_MAJOR;
    sb.version_minor    = NBFS_VERSION_MINOR;

    sb.block_size       = NBFS_DEFAULT_BLOCK_SIZE;

    sb.total_blocks     = (128ULL*1024ULL*1024ULL) /
                          NBFS_DEFAULT_BLOCK_SIZE;

    sb.root_inode       = 1;

    sb.journal_start    = 3;
    sb.journal_blocks   = 64;

    sb.block_bitmap_start = 67;
    sb.inode_bitmap_start = 68;
    sb.inode_table_start  = 69;
    sb.data_start         = 128;

    strcpy(sb.volume_name,"NeoBench");

    fseek(fp,
          NBFS_DEFAULT_BLOCK_SIZE,
          SEEK_SET);

    fwrite(&sb,
           sizeof(sb),
           1,
           fp);

    return 0;
}
