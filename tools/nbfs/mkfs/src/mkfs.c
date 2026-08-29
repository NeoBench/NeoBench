#include <stdio.h>

#include "image.h"
#include "fs/superblock.h"
#include "mkfs.h"
#include "fs/bootblock.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/rootdir.h"
#include "fs/directory.h"
#include "fs/verify.h"

static uint64_t g_image_size =
    128ULL * 1024ULL * 1024ULL;

uint64_t mkfs_image_size(void)
{
    return g_image_size;
}

int mkfs_create_ex(const char *image, uint64_t size_bytes)
{
    FILE *fp;

    if (size_bytes == 0 ||
        size_bytes % NBFS_DEFAULT_BLOCK_SIZE != 0)
    {
        puts("Invalid image size.");
        return 1;
    }

    if (size_bytes <
        (NBFS_DATA_START + 1) * NBFS_DEFAULT_BLOCK_SIZE)
    {
        puts("Image too small for NBFS v1 layout.");
        return 1;
    }

    g_image_size = size_bytes;

    fp = image_create(image, size_bytes);

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
     * This allocates the first data block:
     * NBFS_DATA_START (324).
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
     * Includes:
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
     * 4. Write superblock.
     *
     * This is done after root allocation so
     * free_blocks reflects the actual image state.
     */
    if (nbfs_write_superblock(fp) != 0)
    {
        puts("Failed to write superblock.");
        fclose(fp);
        return 1;
    }

    /*
     * 5. Write root directory.
     */
    if (nbfs_write_root_directory(fp, NBFS_DATA_START) != 0)
    {
        puts("Failed to write root directory.");
        fclose(fp);
        return 1;
    }

    /*
     * 6. Verify the completed filesystem.
     */
    if (nbfs_verify_image(fp) != 0)
    {
        puts("NBFS filesystem verification failed.");
        fclose(fp);
        return 1;
    }

    fclose(fp);

    puts("NBFS filesystem created.");

    return 0;
}

int mkfs_create(const char *image)
{
    return mkfs_create_ex(image, 128ULL * 1024ULL * 1024ULL);
}
