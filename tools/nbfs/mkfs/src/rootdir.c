/*
 * rootdir.c
 *
 * NeoBench mkfs.nbfs
 *
 * Root inode creation
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/inode.h"
#include "fs/bitmap.h"
#include "fs/directory.h"


int nbfs_create_root_inode(FILE *fp)
{
    nbfs_inode_t root;

    uint64_t root_block;


    memset(&root, 0, sizeof(root));


    root.inode_number = 1;


    /*
     * Root directory inode
     */
    root.mode = 0x4000;
    root.links = 1;



    /*
     * Allocate first filesystem data block.
     *
     * Layout v1:
     *
     * 0       boot
     * 1       superblock
     * 2       block bitmap
     * 3       inode bitmap
     * 4-67    inode table
     * 68-323  journal
     * 324+    data
     */
    root_block = nbfs_alloc_block();


    if (root_block == UINT64_MAX)
    {
        puts("Failed to allocate root directory block.");
        return -1;
    }



    /*
     * Root directory is one block.
     */
    root.size =
        NBFS_DEFAULT_BLOCK_SIZE;


    root.extents[0].start_block =
        root_block;

    root.extents[0].block_count =
        1;

    root.extents[0].flags =
        0;



    /*
     * Write directory contents first.
     *
     * Creates:
     *
     * .
     * ..
     */
    if (nbfs_write_root_directory(
            fp,
            root_block) != 0)
    {
        puts("Failed to write root directory.");
        return -1;
    }



    /*
     * Write inode 1.
     */
    if (nbfs_write_inode(
            fp,
            root.inode_number,
            &root) != 0)
    {
        puts("Failed to write root inode.");
        return -1;
    }



    /*
     * Mark inode bitmap.
     */
    if (nbfs_write_inode_bitmap(fp) != 0)
    {
        puts("Failed to write inode bitmap.");
        return -1;
    }


    return 0;
}
