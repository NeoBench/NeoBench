#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define IMAGE "../../images/test-verified.nbfs"

static int fail(const char *msg)
{
    printf("FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t docs = 0;
    uint64_t file = 0;
    uint64_t resolved = 0;

    const char *data = "NeoBench NBFS integration test";
    char buffer[128];

    printf("NeoBench libNBFS integration test\n");
    printf("=================================\n\n");

    ctx = nbfs_open(IMAGE);
    if (!ctx)
        return fail("could not open verified NBFS image");

    printf("PASS: open verified filesystem\n");

    /*
     * Create a directory.
     */
    if (nbfs_create_directory(ctx, 1, "integration") != 0)
        return fail("could not create integration directory");

    if (nbfs_lookup(ctx, 1, "integration", &docs) != 0)
        return fail("could not lookup integration directory");

    printf("PASS: created /integration -> inode %llu\n",
           (unsigned long long)docs);

    /*
     * Create a file.
     */
    if (nbfs_create_file(ctx, docs, "hello.txt") != 0)
        return fail("could not create integration file");

    if (nbfs_lookup(ctx, docs, "hello.txt", &file) != 0)
        return fail("could not lookup hello.txt");

    printf("PASS: created /integration/hello.txt -> inode %llu\n",
           (unsigned long long)file);

    /*
     * Write/read file data.
     */
    if (nbfs_write_file(ctx, file, data, strlen(data)) != 0)
        return fail("could not write integration file");

    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(ctx, file, buffer, strlen(data)) != 0)
        return fail("could not read integration file");

    if (memcmp(buffer, data, strlen(data)) != 0)
        return fail("file contents do not match");

    printf("PASS: file write/read data matches\n");

    /*
     * Absolute pathname resolution.
     */
    if (nbfs_resolve_path(
            ctx,
            1,
            "/integration/hello.txt",
            &resolved) != 0)
        return fail("absolute path resolution failed");

    if (resolved != file)
        return fail("absolute path resolved to wrong inode");

    printf("PASS: absolute path resolves correctly\n");

    /*
     * Relative pathname resolution.
     */
    if (nbfs_resolve_path(
            ctx,
            docs,
            "hello.txt",
            &resolved) != 0)
        return fail("relative path resolution failed");

    if (resolved != file)
        return fail("relative path resolved to wrong inode");

    printf("PASS: relative path resolves correctly\n");

    /*
     * Flush and reopen.
     */
    if (nbfs_flush(ctx) != 0)
        return fail("filesystem flush failed");

    nbfs_close(ctx);

    printf("PASS: filesystem flushed and closed\n");

    ctx = nbfs_open(IMAGE);
    if (!ctx)
        return fail("filesystem reopen failed");

    printf("PASS: filesystem reopened\n");

    /*
     * Verify persistence.
     */
    if (nbfs_resolve_path(
            ctx,
            1,
            "/integration/hello.txt",
            &resolved) != 0)
        return fail("persisted path resolution failed");

    if (resolved != file)
        return fail("persisted path resolved to wrong inode");

    printf("PASS: pathname survives reopen\n");

    memset(buffer, 0, sizeof(buffer));

    if (nbfs_read_file(ctx, file, buffer, strlen(data)) != 0)
        return fail("persisted file read failed");

    if (memcmp(buffer, data, strlen(data)) != 0)
        return fail("persisted file contents do not match");

    printf("PASS: file contents survive reopen\n");

    nbfs_close(ctx);

    printf("\nNeoBench libNBFS integration test: OK\n");
    printf("PASS\n");

    return 0;
}
