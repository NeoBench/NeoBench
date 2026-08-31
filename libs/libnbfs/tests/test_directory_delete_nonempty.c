#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-directory-delete-nonempty.nbfs"

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

    printf("NBFS non-empty directory deletion test\n");
    printf("=======================================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create docs.
     */
    if (nbfs_create_directory(
            ctx,
            1,
            "docs") != 0)
    {
        nbfs_close(ctx);
        return fail("create docs directory failed");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &dir_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs failed");
    }

    printf(
        "PASS: created docs -> inode %llu\n",
        (unsigned long long)dir_inode);

    /*
     * Put a file inside docs.
     */
    if (nbfs_create_file(
            ctx,
            dir_inode,
            "keep.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create keep.txt failed");
    }

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "keep.txt",
            &file_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup keep.txt failed");
    }

    printf(
        "PASS: created docs/keep.txt -> inode %llu\n",
        (unsigned long long)file_inode);

    /*
     * Directory must NOT be removable while non-empty.
     */
    if (nbfs_delete_directory(
            ctx,
            dir_inode) == 0)
    {
        nbfs_close(ctx);
        return fail("non-empty directory was deleted");
    }

    printf(
        "PASS: non-empty directory deletion rejected\n");

    /*
     * docs must still exist.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("docs disappeared after rejected deletion");
    }

    if (found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("docs inode changed after rejected deletion");
    }

    printf(
        "PASS: directory remains intact\n");

    /*
     * File must still exist.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            "keep.txt",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("keep.txt disappeared after rejected deletion");
    }

    if (found != file_inode)
    {
        nbfs_close(ctx);
        return fail("keep.txt inode changed");
    }

    printf(
        "PASS: child file remains intact\n");

    /*
     * Verify the state survives close/reopen.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("reopen failed");

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("docs missing after reopen");
    }

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "keep.txt",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("keep.txt missing after reopen");
    }

    printf(
        "PASS: rejected deletion survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS non-empty directory deletion: OK\n");

    return 0;
}
