#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-libnbfs.nbfs"

#define GOOD_SIZE (12 * NBFS_DEFAULT_BLOCK_SIZE)
#define BAD_SIZE  (13 * NBFS_DEFAULT_BLOCK_SIZE)

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

    unsigned char *good_buffer;
    unsigned char *bad_buffer;
    unsigned char *read_buffer;

    printf("NBFS extent limit test\n");
    printf("======================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    good_buffer = malloc(GOOD_SIZE);
    bad_buffer = malloc(BAD_SIZE);
    read_buffer = malloc(GOOD_SIZE);

    if (!good_buffer || !bad_buffer || !read_buffer)
    {
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("unable to allocate test buffers");
    }

    fill_pattern(good_buffer, GOOD_SIZE);
    fill_pattern(bad_buffer, BAD_SIZE);
    memset(read_buffer, 0, GOOD_SIZE);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
    {
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("unable to open NBFS image");
    }

    if (nbfs_create_file(
            ctx,
            1,
            "limit.bin") != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("create limit.bin failed");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "limit.bin",
            &inode_number) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("lookup limit.bin failed");
    }

    printf(
        "PASS: created limit.bin -> inode %llu\n",
        (unsigned long long)inode_number);

    /*
     * Exactly 12 blocks must be accepted.
     */
    if (nbfs_write_file(
            ctx,
            inode_number,
            good_buffer,
            GOOD_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("12-block write failed");
    }

    printf("PASS: 12-block file accepted\n");

    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("unable to read inode");
    }

    if (inode.size != GOOD_SIZE)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("12-block inode size incorrect");
    }

    /*
     * All 12 extent slots must be occupied.
     */
    for (unsigned int i = 0;
         i < NBFS_EXTENTS_PER_INODE;
         i++)
    {
        if (inode.extents[i].block_count != 1)
        {
            nbfs_close(ctx);
            free(good_buffer);
            free(bad_buffer);
            free(read_buffer);
            return fail("not all 12 extents allocated");
        }
    }

    printf("PASS: all 12 extent slots allocated\n");

    /*
     * Verify the 12-block file.
     */
    if (nbfs_read_file(
            ctx,
            inode_number,
            read_buffer,
            GOOD_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("12-block read failed");
    }

    if (memcmp(
            good_buffer,
            read_buffer,
            GOOD_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("12-block data mismatch");
    }

    printf("PASS: 12-block data matches\n");

    /*
     * A 13-block write must be rejected.
     */
    if (nbfs_write_file(
            ctx,
            inode_number,
            bad_buffer,
            BAD_SIZE) == 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("13-block write was incorrectly accepted");
    }

    printf("PASS: 13-block write correctly rejected\n");

    /*
     * Most importantly, the failed oversized write must not
     * destroy the existing 12-block file.
     */
    memset(read_buffer, 0, GOOD_SIZE);

    if (nbfs_read_file(
            ctx,
            inode_number,
            read_buffer,
            GOOD_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("read after rejected write failed");
    }

    if (memcmp(
            good_buffer,
            read_buffer,
            GOOD_SIZE) != 0)
    {
        nbfs_close(ctx);
        free(good_buffer);
        free(bad_buffer);
        free(read_buffer);
        return fail("existing data was damaged by rejected write");
    }

    printf("PASS: existing 12-block data preserved\n");

    nbfs_close(ctx);

    free(good_buffer);
    free(bad_buffer);
    free(read_buffer);

    printf("\nNBFS extent limit: OK\n");

    return 0;
}
