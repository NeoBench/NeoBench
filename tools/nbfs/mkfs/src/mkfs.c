#include <stdio.h>

#include "image.h"
#include "fs/superblock.h"
#include "mkfs.h"
#include "fs/bootblock.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/rootdir.h"
#include "fs/directory.h"

int mkfs_create(const char *image)
{
    FILE *fp =
        image_create(
            image,
            128ULL * 1024ULL * 1024ULL);

    if (!fp)
    {
        puts("Unable to create image.");
        return 1;
    }

    /*
     * 1. Boot block
     */
    if (nbfs_write_bootblock(fp) != 0)
    {
        puts("Failed to write boot block.");
        fclose(fp);
        return 1;
    }

    /*
     * 2. Create root inode.
     *
     * This allocates the first DATA block (324).
     */
    if (nbfs_create_root_inode(fp) != 0)
    {
        puts("Failed to create root inode.");
        fclose(fp);
        return 1;
    }

    /*
     * 3. Write block bitmap.
     *
     * This now includes:
     *   - reserved blocks 0-323
     *   - allocated root directory block 324
     */
    if (nbfs_write_block_bitmap(fp) != 0)
    {
        puts("Failed to write block bitmap.");
        fclose(fp);
        return 1;
    }

    /*
     * 4. Superblock.
     *
     * Written after root allocation so free_blocks
     * reflects the actual filesystem state.
     */
    if (nbfs_write_superblock(fp) != 0)
    {
        puts("Failed to write superblock.");
        fclose(fp);
        return 1;
    }

    fclose(fp);

    puts("NBFS filesystem created.");

    return 0;
}
