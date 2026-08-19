#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define TEST_IMAGE "../../images/test-libnbfs.nbfs"

static int failures = 0;

static void check(int condition, const char *message)
{
    if (condition)
        printf("PASS: %s\n", message);
    else
    {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    nbfs_context_t *ctx;
    nbfs_superblock_t sb;
    nbfs_inode_t inode;

    uint64_t block = UINT64_MAX;
    uint64_t inode_number = UINT64_MAX;

    puts("NeoBench libNBFS filesystem test");
    puts("================================");
    puts("");

    /*
     * Open the known-good filesystem copy.
     */
    ctx = nbfs_open(TEST_IMAGE);

    check(ctx != NULL, "open filesystem");

    if (!ctx)
        return 1;

    /*
     * Read and validate the superblock.
     */
    memset(&sb, 0, sizeof(sb));

    check(
        nbfs_read_superblock(ctx, &sb) == 0,
        "read superblock"
    );

    check(
        sb.magic == NBFS_MAGIC,
        "superblock magic"
    );

    check(
        sb.block_size == NBFS_DEFAULT_BLOCK_SIZE,
        "block size"
    );

    check(
        sb.root_inode == 1,
        "root inode"
    );

    /*
     * Read the existing root inode.
     */
    memset(&inode, 0, sizeof(inode));

    check(
        nbfs_read_inode(ctx, sb.root_inode, &inode) == 0,
        "read root inode"
    );

    check(
        inode.inode_number == 1,
        "root inode number"
    );

    check(
        inode.extents[0].start_block == NBFS_DATA_START,
        "root directory starts at data block"
    );

    /*
     * Test block allocation.
     */
    check(
        nbfs_allocate_block(ctx, &block) == 0,
        "allocate block"
    );

    check(
        block >= NBFS_DATA_START,
        "allocated block is in data area"
    );

    /*
     * Test inode allocation.
     */
    check(
        nbfs_allocate_inode(ctx, &inode_number) == 0,
        "allocate inode"
    );

    check(
        inode_number != 0 && inode_number != sb.root_inode,
        "allocated inode is not root inode"
    );

    /*
     * Build an inode and write it.
     */
    memset(&inode, 0, sizeof(inode));

    inode.inode_number = inode_number;
    inode.mode = 0x8000;       /* regular file */
    inode.links = 1;
    inode.size = 0;

    inode.extents[0].start_block = block;
    inode.extents[0].block_count = 1;
    inode.extents[0].flags = 0;

    check(
        nbfs_write_inode(ctx, &inode) == 0,
        "write allocated inode"
    );

    /*
     * Read it back and verify persistence through the library.
     */
    memset(&inode, 0, sizeof(inode));

    check(
        nbfs_read_inode(ctx, inode_number, &inode) == 0,
        "read allocated inode"
    );

    check(
        inode.inode_number == inode_number,
        "inode number persisted"
    );

    check(
        inode.extents[0].start_block == block,
        "inode extent persisted"
    );

    /*
     * Flush before closing.
     */
    check(
        nbfs_flush(ctx) == 0,
        "flush filesystem"
    );

    nbfs_close(ctx);

    /*
     * Reopen the image and verify the inode again.
     */
    ctx = nbfs_open(TEST_IMAGE);

    check(ctx != NULL, "reopen filesystem");

    if (ctx)
    {
        memset(&inode, 0, sizeof(inode));

        check(
            nbfs_read_inode(ctx, inode_number, &inode) == 0,
            "read inode after reopen"
        );

        check(
            inode.inode_number == inode_number,
            "inode survives reopen"
        );

        check(
            inode.extents[0].start_block == block,
            "extent survives reopen"
        );

        /*
         * Free the resources used by this test.
         */
        check(
            nbfs_free_inode(ctx, inode_number) == 0,
            "free test inode"
        );

        check(
            nbfs_free_block(ctx, block) == 0,
            "free test block"
        );

        nbfs_flush(ctx);
        nbfs_close(ctx);
    }

    puts("");

    if (failures)
    {
        printf(
            "libNBFS filesystem test: FAILED (%d failures)\n",
            failures
        );

        return 1;
    }

    puts("libNBFS filesystem test: OK");

    return 0;
}
