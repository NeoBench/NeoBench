#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

    const char message[] = "NeoBench nested directory test";
    char buffer[sizeof(message)];

    printf("NBFS nested directory test\n");
    printf("===========================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create a directory beneath root.
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

    if (dir_inode == 1)
    {
        nbfs_close(ctx);
        return fail("docs incorrectly uses root inode");
    }

    /*
     * "." must resolve to the new directory.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            ".",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs/. failed");
    }

    if (found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("docs/. points to wrong inode");
    }

    printf("PASS: docs/. -> inode %llu\n",
           (unsigned long long)found);

    /*
     * ".." must resolve back to root.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            "..",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs/.. failed");
    }

    if (found != 1)
    {
        nbfs_close(ctx);
        return fail("docs/.. does not point to root");
    }

    printf("PASS: docs/.. -> inode 1\n");

    /*
     * Create a file inside docs.
     */
    if (nbfs_create_file(
            ctx,
            dir_inode,
            "readme.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create nested file failed");
    }

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "readme.txt",
            &file_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup nested file failed");
    }

    printf(
        "PASS: created docs/readme.txt -> inode %llu\n",
        (unsigned long long)file_inode);

    /*
     * Write and read the nested file.
     */
    if (nbfs_write_file(
            ctx,
            file_inode,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("write nested file failed");
    }

    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            file_inode,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("read nested file failed");
    }

    if (memcmp(
            buffer,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("nested file data mismatch");
    }

    printf("PASS: nested file data matches\n");

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Reopen and verify hierarchy + data.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to reopen NBFS image");

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("docs missing after reopen");
    }

    if (found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("docs inode changed after reopen");
    }

    if (nbfs_lookup(
            ctx,
            dir_inode,
            "readme.txt",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("nested file missing after reopen");
    }

    if (found != file_inode)
    {
        nbfs_close(ctx);
        return fail("nested file inode changed after reopen");
    }

    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            file_inode,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent nested read failed");
    }

    if (memcmp(
            buffer,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent nested data mismatch");
    }

    printf("PASS: nested hierarchy survives close/reopen\n");
    printf("PASS: nested file data survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS nested directory: OK\n");

    return 0;
}
