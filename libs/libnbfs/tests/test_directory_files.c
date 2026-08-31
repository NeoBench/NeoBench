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

static int check_lookup(
    nbfs_context_t *ctx,
    const char *name,
    uint64_t expected_inode)
{
    uint64_t inode = 0;

    if (nbfs_lookup(ctx, 1, name, &inode) != 0)
        return -1;

    if (inode != expected_inode)
        return -1;

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;

    uint64_t inode_a = 0;
    uint64_t inode_b = 0;
    uint64_t inode_c = 0;

    printf("NBFS multiple directory files test\n");
    printf("==================================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create three independent files.
     */
    if (nbfs_create_file(ctx, 1, "alpha.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create alpha.txt failed");
    }

    if (nbfs_create_file(ctx, 1, "beta.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create beta.txt failed");
    }

    if (nbfs_create_file(ctx, 1, "gamma.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create gamma.txt failed");
    }

    if (nbfs_lookup(ctx, 1, "alpha.txt", &inode_a) != 0 ||
        nbfs_lookup(ctx, 1, "beta.txt", &inode_b) != 0 ||
        nbfs_lookup(ctx, 1, "gamma.txt", &inode_c) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup of multiple files failed");
    }

    printf(
        "PASS: alpha.txt -> inode %llu\n",
        (unsigned long long)inode_a);

    printf(
        "PASS: beta.txt -> inode %llu\n",
        (unsigned long long)inode_b);

    printf(
        "PASS: gamma.txt -> inode %llu\n",
        (unsigned long long)inode_c);

    if (inode_a == inode_b ||
        inode_a == inode_c ||
        inode_b == inode_c)
    {
        nbfs_close(ctx);
        return fail("files received duplicate inodes");
    }

    printf("PASS: files have distinct inodes\n");

    /*
     * Delete the middle file.
     */
    if (nbfs_delete_file(ctx, inode_b) != 0)
    {
        nbfs_close(ctx);
        return fail("delete beta.txt failed");
    }

    printf("PASS: deleted beta.txt\n");

    /*
     * beta.txt must be gone.
     */
    if (nbfs_lookup(ctx, 1, "beta.txt", &inode_b) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted beta.txt still found");
    }

    printf("PASS: beta.txt no longer found\n");

    /*
     * alpha.txt and gamma.txt must remain untouched.
     */
    if (check_lookup(ctx, "alpha.txt", inode_a) != 0)
    {
        nbfs_close(ctx);
        return fail("alpha.txt was corrupted");
    }

    if (check_lookup(ctx, "gamma.txt", inode_c) != 0)
    {
        nbfs_close(ctx);
        return fail("gamma.txt was corrupted");
    }

    printf("PASS: remaining directory entries intact\n");

    /*
     * Create another file after deleting the middle entry.
     */
    if (nbfs_create_file(ctx, 1, "delta.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create delta.txt failed");
    }

    if (nbfs_lookup(ctx, 1, "delta.txt", &inode_b) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup delta.txt failed");
    }

    printf(
        "PASS: delta.txt -> inode %llu\n",
        (unsigned long long)inode_b);

    /*
     * Verify all live files again.
     */
    if (check_lookup(ctx, "alpha.txt", inode_a) != 0 ||
        check_lookup(ctx, "gamma.txt", inode_c) != 0 ||
        check_lookup(ctx, "delta.txt", inode_b) != 0)
    {
        nbfs_close(ctx);
        return fail("directory entries corrupted after reuse");
    }

    printf("PASS: directory remains consistent after reuse\n");

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Verify persistence.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to reopen NBFS image");

    if (check_lookup(ctx, "alpha.txt", inode_a) != 0 ||
        check_lookup(ctx, "gamma.txt", inode_c) != 0 ||
        check_lookup(ctx, "delta.txt", inode_b) != 0)
    {
        nbfs_close(ctx);
        return fail("directory state did not survive reopen");
    }

    if (nbfs_lookup(ctx, 1, "beta.txt", &inode_c) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted beta.txt returned after reopen");
    }

    printf("PASS: directory state survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS multiple directory files: OK\n");

    return 0;
}
