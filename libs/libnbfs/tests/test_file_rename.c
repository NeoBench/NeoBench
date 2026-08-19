#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-file-rename.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int reset_image(void)
{
    FILE *src = fopen(SOURCE_IMAGE, "rb");
    FILE *dst;
    unsigned char buffer[8192];
    size_t n;

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
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t old_inode = 0;
    uint64_t new_inode = 0;

    printf("NBFS same-directory rename test\n");
    printf("================================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    if (nbfs_create_file(
            ctx,
            1,
            "old.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create old.txt failed");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &old_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup old.txt failed");
    }

    printf(
        "PASS: created old.txt -> inode %llu\n",
        (unsigned long long)old_inode);

    if (nbfs_rename(
            ctx,
            1,
            "old.txt",
            "new.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("rename old.txt -> new.txt failed");
    }

    printf("PASS: rename returned success\n");

    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &new_inode) == 0)
    {
        nbfs_close(ctx);
        return fail("old.txt still exists");
    }

    printf("PASS: old.txt no longer found\n");

    if (nbfs_lookup(
            ctx,
            1,
            "new.txt",
            &new_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("new.txt not found");
    }

    if (new_inode != old_inode)
    {
        nbfs_close(ctx);
        return fail("rename changed inode");
    }

    printf(
        "PASS: new.txt -> same inode %llu\n",
        (unsigned long long)new_inode);

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
            "old.txt",
            &new_inode) == 0)
    {
        nbfs_close(ctx);
        return fail("old.txt returned after reopen");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "new.txt",
            &new_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("new.txt missing after reopen");
    }

    if (new_inode != old_inode)
    {
        nbfs_close(ctx);
        return fail("inode changed after reopen");
    }

    printf("PASS: rename survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS same-directory rename: OK\n");

    return 0;
}
