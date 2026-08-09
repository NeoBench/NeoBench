#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define IMAGE        "../../images/test-libnbfs.nbfs"

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

    dst = fopen(IMAGE, "wb");

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

    if (ferror(src))
    {
        fclose(dst);
        fclose(src);
        return -1;
    }

    fclose(dst);
    fclose(src);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t inode = 0;
    uint64_t found = 0;

    const char message[] =
        "Hello from NeoBench NBFS file I/O!";

    char buffer[sizeof(message)];

    printf("NBFS file I/O test\n");
    printf("==================\n");

    /*
     * Always start from a clean filesystem image.
     *
     * This prevents hello.txt left by another test from
     * causing nbfs_create_file() to fail.
     */
    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create the test file.
     */
    if (nbfs_create_file(ctx, 1, "hello.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create file failed");
    }

    /*
     * Look up the newly-created file.
     */
    if (nbfs_lookup(ctx, 1, "hello.txt", &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup after create failed");
    }

    printf(
        "PASS: created hello.txt -> inode %llu\n",
        (unsigned long long)inode);

    /*
     * Write test data.
     */
    if (nbfs_write_file(
            ctx,
            inode,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("write failed");
    }

    printf(
        "PASS: wrote %zu bytes\n",
        sizeof(message));

    /*
     * Read the data back.
     */
    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            inode,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("read failed");
    }

    if (memcmp(
            buffer,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("read data does not match");
    }

    printf("PASS: read data matches\n");

    /*
     * Flush changes to disk.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Reopen the image and verify persistence.
     */
    ctx = nbfs_open(IMAGE);

    if (!ctx)
        return fail("unable to reopen NBFS image");

    if (nbfs_lookup(
            ctx,
            1,
            "hello.txt",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup after reopen failed");
    }

    if (found != inode)
    {
        nbfs_close(ctx);
        return fail("inode changed after reopen");
    }

    printf(
        "PASS: reopened hello.txt -> inode %llu\n",
        (unsigned long long)found);

    /*
     * Verify persistent file contents.
     */
    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(
            ctx,
            inode,
            buffer,
            sizeof(buffer)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent read failed");
    }

    if (memcmp(
            buffer,
            message,
            sizeof(message)) != 0)
    {
        nbfs_close(ctx);
        return fail("persistent data does not match");
    }

    printf("PASS: data survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS file I/O: OK\n");

    return 0;
}
