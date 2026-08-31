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
    uint64_t inode = 0;

    printf("NBFS file creation test\n");
    printf("=======================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create a file in the root directory.
     */
    if (nbfs_create_file(
            ctx,
            1,
            "hello.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("file creation failed");
    }

    printf("PASS: created hello.txt\n");

    /*
     * Find the new directory entry.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "hello.txt",
            &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("created file cannot be looked up");
    }

    printf("PASS: lookup hello.txt -> inode %llu\n",
           (unsigned long long)inode);

    if (inode != 2)
    {
        nbfs_close(ctx);
        return fail("created file did not receive inode 2");
    }

    /*
     * Verify the inode itself.
     */
    {
        nbfs_inode_t file_inode;

        if (nbfs_read_inode(
                ctx,
                inode,
                &file_inode) != 0)
        {
            nbfs_close(ctx);
            return fail("unable to read created inode");
        }

        if (file_inode.inode_number != inode)
        {
            nbfs_close(ctx);
            return fail("inode number mismatch");
        }

        if (file_inode.mode != 0x8000)
        {
            nbfs_close(ctx);
            return fail("created inode is not a regular file");
        }

        if (file_inode.size != 0)
        {
            nbfs_close(ctx);
            return fail("new file is not zero length");
        }

        printf("PASS: inode 2 is a zero-length regular file\n");
    }

    nbfs_close(ctx);

    printf("\nNBFS file creation: OK\n");

    return 0;
}
