#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define IMAGE "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t inode = 0;
    uint64_t found = 0;

    printf("NBFS file deletion test\n");
    printf("=======================\n");

    ctx = nbfs_open(IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create a file.
     */
    if (nbfs_create_file(ctx, 1, "delete.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create file failed");
    }

    if (nbfs_lookup(ctx, 1, "delete.txt", &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup after create failed");
    }

    printf("PASS: created delete.txt -> inode %llu\n",
           (unsigned long long)inode);

    /*
     * Delete it.
     */
    if (nbfs_delete_file(ctx, inode) != 0)
    {
        nbfs_close(ctx);
        return fail("delete file failed");
    }

    printf("PASS: delete_file returned success\n");

    /*
     * The directory entry must no longer be visible.
     */
    if (nbfs_lookup(ctx, 1, "delete.txt", &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted file is still visible");
    }

    printf("PASS: deleted file no longer found\n");

    /*
     * The inode should be reusable.
     */
    if (nbfs_allocate_inode(ctx, &found) != 0)
    {
        nbfs_close(ctx);
        return fail("inode allocation after delete failed");
    }

    if (found != inode)
    {
        nbfs_close(ctx);
        return fail("deleted inode was not reclaimed");
    }

    printf("PASS: inode %llu reclaimed\n",
           (unsigned long long)found);

    /*
     * Return the inode to the free pool so this test
     * does not leave the image with an artificial allocation.
     */
    if (nbfs_free_inode(ctx, found) != 0)
    {
        nbfs_close(ctx);
        return fail("cleanup inode free failed");
    }

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    printf("\nNBFS file deletion: OK\n");

    return 0;
}
