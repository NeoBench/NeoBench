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
    uint64_t inode = 0;

    const char old_data[] =
        "Old NeoBench NBFS contents.";

    const char new_data[] =
        "New NeoBench NBFS contents after overwrite.";

    char buffer[sizeof(new_data)];

    printf("NBFS file overwrite test\n");
    printf("========================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create the file.
     */
    if (nbfs_create_file(ctx, 1, "overwrite.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create file failed");
    }

    if (nbfs_lookup(ctx, 1, "overwrite.txt", &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup failed");
    }

    printf(
        "PASS: created overwrite.txt -> inode %llu\n",
        (unsigned long long)inode);

    /*
     * Initial write.
     */
    if (nbfs_write_file(
            ctx,
            inode,
            old_data,
            sizeof(old_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("initial write failed");
    }

    printf(
        "PASS: wrote initial data (%zu bytes)\n",
        sizeof(old_data));

    /*
     * Replace the contents.
     */
    if (nbfs_write_file(
            ctx,
            inode,
            new_data,
            sizeof(new_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("overwrite failed");
    }

    printf(
        "PASS: overwrote file (%zu bytes)\n",
        sizeof(new_data));

    /*
     * Read the new contents.
     */
    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            inode,
            0,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("read after overwrite failed");
    }

    if (memcmp(
            buffer,
            new_data,
            sizeof(new_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("overwritten data does not match");
    }

    printf("PASS: overwritten data matches\n");

    /*
     * Ensure the old contents are gone.
     */
    if (memcmp(
            buffer,
            old_data,
            sizeof(old_data)) == 0)
    {
        nbfs_close(ctx);
        return fail("old data is still present");
    }

    printf("PASS: old contents replaced\n");

    /*
     * Flush and reopen.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to reopen NBFS image");

    if (nbfs_lookup(ctx, 1, "overwrite.txt", &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup after reopen failed");
    }

    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            inode,
            0,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent read failed");
    }

    if (memcmp(
            buffer,
            new_data,
            sizeof(new_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent overwrite data does not match");
    }

    printf("PASS: overwritten data survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS file overwrite: OK\n");

    return 0;
}
