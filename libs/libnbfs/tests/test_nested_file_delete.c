#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int reset_image(void)
{
    FILE *src;
    FILE *dst;
    unsigned char buffer[8192];
    size_t n;

    src = fopen(SOURCE_IMAGE, "rb");
    if (!src)
        return -1;

    dst = fopen(TEST_IMAGE, "wb");
    if (!dst)
    {
        fclose(src);
        return -1;
    }

    while ((n = fread(buffer, 1, sizeof(buffer), src)) != 0)
    {
        if (fwrite(buffer, 1, n, dst) != n)
        {
            fclose(dst);
            fclose(src);
            return -1;
        }
    }

    fclose(dst);
    fclose(src);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t dir_inode = 0;
    uint64_t file_inode = 0;
    uint64_t found = 0;

    printf("NBFS nested file deletion test\n");
    printf("===============================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create subdirectory.
     */
    if (nbfs_create_directory(ctx, 1, "docs") != 0)
    {
        nbfs_close(ctx);
        return fail("create docs failed");
    }

    if (nbfs_lookup(ctx, 1, "docs", &dir_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs failed");
    }

    printf("PASS: created docs -> inode %llu\n",
           (unsigned long long)dir_inode);

    /*
     * Create file inside docs.
     */
    if (nbfs_create_file(ctx, dir_inode, "delete.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create nested file failed");
    }

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "delete.txt",
            &file_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup nested file failed");
    }

    printf("PASS: created docs/delete.txt -> inode %llu\n",
           (unsigned long long)file_inode);

    /*
     * Delete nested file.
     */
    if (nbfs_delete_file(ctx, file_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("delete nested file failed");
    }

    printf("PASS: nested delete returned success\n");

    /*
     * It must no longer be visible inside docs.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            "delete.txt",
            &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted nested file still found");
    }

    printf("PASS: deleted nested file no longer found\n");

    /*
     * The directory itself must still work.
     */
    if (nbfs_lookup(ctx, dir_inode, ".", &found) != 0)
    {
        nbfs_close(ctx);
        return fail("docs/. broken after deletion");
    }

    if (found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("docs/. points to wrong inode");
    }

    if (nbfs_lookup(ctx, dir_inode, "..", &found) != 0)
    {
        nbfs_close(ctx);
        return fail("docs/.. broken after deletion");
    }

    if (found != 1)
    {
        nbfs_close(ctx);
        return fail("docs/.. points to wrong inode");
    }

    printf("PASS: nested directory remains valid\n");

    /*
     * Reopen and verify deletion persisted.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to reopen image");

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "delete.txt",
            &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted nested file returned after reopen");
    }

    if (nbfs_lookup(ctx, dir_inode, ".", &found) != 0 ||
        found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("nested . corrupted after reopen");
    }

    if (nbfs_lookup(ctx, dir_inode, "..", &found) != 0 ||
        found != 1)
    {
        nbfs_close(ctx);
        return fail("nested .. corrupted after reopen");
    }

    printf("PASS: deletion survives close/reopen\n");
    printf("PASS: . and .. survive nested deletion\n");

    nbfs_close(ctx);

    printf("\nNBFS nested file deletion: OK\n");

    return 0;
}
