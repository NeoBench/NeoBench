#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-libnbfs.nbfs"

#define TEST_SIZE (8 * NBFS_DEFAULT_BLOCK_SIZE)

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

static void fill_pattern(
    unsigned char *buffer,
    size_t size)
{
    size_t i;

    for (i = 0; i < size; i++)
        buffer[i] = (unsigned char)(i % 251);
}

int main(void)
{
    nbfs_context_t *ctx;
    nbfs_inode_t inode;
    uint64_t inode_number = 0;

    unsigned char *write_buffer;
    unsigned char *read_buffer;

    printf("NBFS multi-block file test\n");
    printf("==========================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    write_buffer = malloc(TEST_SIZE);
    read_buffer = malloc(TEST_SIZE);

    if (!write_buffer || !read_buffer)
    {
        free(write_buffer);
        free(read_buffer);
        return fail("unable to allocate test buffers");
    }

    fill_pattern(write_buffer, TEST_SIZE);
    memset(read_buffer, 0, TEST_SIZE);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
    {
        free(write_buffer);
        free(read_buffer);
        return fail("unable to open NBFS image");
    }

    /*
     * Create the test file.
     */
    if (nbfs_create_file(
            ctx,
            1,
            "multiblock.bin") != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("create file failed");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "multiblock.bin",
            &inode_number) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("lookup failed");
    }

    printf(
        "PASS: created multiblock.bin -> inode %llu\n",
        (unsigned long long)inode_number);

    /*
     * Write 32 KiB.
     */
    if (nbfs_write_file(
            ctx,
            inode_number,
            write_buffer,
            TEST_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("multi-block write failed");
    }

    printf(
        "PASS: wrote %u bytes\n",
        (unsigned)TEST_SIZE);

    /*
     * Inspect inode allocation.
     */
    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("unable to read inode");
    }

    if (inode.size != TEST_SIZE)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("inode size is incorrect");
    }

    if (inode.extents[0].block_count == 0 ||
        inode.extents[1].block_count == 0 ||
        inode.extents[2].block_count == 0 ||
        inode.extents[3].block_count == 0 ||
        inode.extents[4].block_count == 0 ||
        inode.extents[5].block_count == 0 ||
        inode.extents[6].block_count == 0 ||
        inode.extents[7].block_count == 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("expected eight allocated extents");
    }

    printf("PASS: eight data extents allocated\n");

    /*
     * Read the complete file.
     */
    if (nbfs_read_file(
            ctx,
            inode_number,
            read_buffer,
            TEST_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("multi-block read failed");
    }

    if (memcmp(
            write_buffer,
            read_buffer,
            TEST_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("multi-block data does not match");
    }

    printf("PASS: all multi-block data matches\n");

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Reopen and verify persistence.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
    {
        free(write_buffer);
        free(read_buffer);
        return fail("unable to reopen NBFS image");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "multiblock.bin",
            &inode_number) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("lookup after reopen failed");
    }

    memset(read_buffer, 0, TEST_SIZE);

    if (nbfs_read_file(
            ctx,
            inode_number,
            read_buffer,
            TEST_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("persistent multi-block read failed");
    }

    if (memcmp(
            write_buffer,
            read_buffer,
            TEST_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(write_buffer);
        free(read_buffer);
        return fail("persistent multi-block data mismatch");
    }

    printf(
        "PASS: %u bytes survive close/reopen\n",
        (unsigned)TEST_SIZE);

    nbfs_close(ctx);

    free(write_buffer);
    free(read_buffer);

    printf("\nNBFS multi-block file: OK\n");

    return 0;
}
