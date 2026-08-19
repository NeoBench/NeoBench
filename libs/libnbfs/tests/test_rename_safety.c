#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"
#include <string.h>

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-rename-safety.nbfs"

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
    uint64_t dir_inode = 0;
    uint64_t found = 0;

    printf("NBFS rename safety test\n");
    printf("=======================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Existing-name collision.
     */
    if (nbfs_create_file(ctx, 1, "old.txt") != 0)
        return fail("create old.txt failed");

    if (nbfs_create_file(ctx, 1, "new.txt") != 0)
        return fail("create new.txt failed");

    if (nbfs_lookup(ctx, 1, "old.txt", &old_inode) != 0)
        return fail("lookup old.txt failed");

    if (nbfs_lookup(ctx, 1, "new.txt", &new_inode) != 0)
        return fail("lookup new.txt failed");

    if (nbfs_rename(
            ctx, 1, "old.txt", "new.txt") == 0)
        return fail("rename over existing name succeeded");

    printf("PASS: rename over existing name rejected\n");

    if (nbfs_lookup(ctx, 1, "old.txt", &found) != 0)
        return fail("old.txt disappeared after rejected rename");

    if (found != old_inode)
        return fail("old.txt inode changed");

    if (nbfs_lookup(ctx, 1, "new.txt", &found) != 0)
        return fail("new.txt disappeared after rejected rename");

    if (found != new_inode)
        return fail("new.txt inode changed");

    printf("PASS: collision left both files intact\n");

    /*
     * Oversized name rejection.
     *
     * NBFS permits names up to NBFS_MAX_NAME_LENGTH bytes.
     * This creates a 256-byte name, which must be rejected.
     */
    {
        char oversized_name[NBFS_MAX_NAME_LENGTH + 2];

        memset(
            oversized_name,
            'x',
            NBFS_MAX_NAME_LENGTH + 1);

        oversized_name[NBFS_MAX_NAME_LENGTH + 1] = '\0';

        if (nbfs_rename(
                ctx,
                1,
                "old.txt",
                oversized_name) == 0)
            return fail("rename to oversized name succeeded");

        printf("PASS: oversized rename rejected\n");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &found) != 0)
        return fail("old.txt disappeared after oversized rename");

    if (found != old_inode)
        return fail("old.txt inode changed");

    printf("PASS: oversized rename left original intact\n");

    /*
     * Directory rename.
     */
    if (nbfs_create_directory(
            ctx, 1, "docs") != 0)
        return fail("create docs failed");

    if (nbfs_lookup(
            ctx, 1, "docs", &dir_inode) != 0)
        return fail("lookup docs failed");

    if (nbfs_rename(
            ctx, 1, "docs", "documents") != 0)
        return fail("directory rename failed");

    printf("PASS: directory rename succeeded\n");

    if (nbfs_lookup(
            ctx, 1, "docs", &found) == 0)
        return fail("old directory name still exists");

    if (nbfs_lookup(
            ctx, 1, "documents", &found) != 0)
        return fail("new directory name missing");

    if (found != dir_inode)
        return fail("directory inode changed");

    if (nbfs_lookup(
            ctx, dir_inode, ".", &found) != 0)
        return fail("renamed directory . failed");

    if (found != dir_inode)
        return fail("renamed directory . is wrong");

    if (nbfs_lookup(
            ctx, dir_inode, "..", &found) != 0)
        return fail("renamed directory .. failed");

    if (found != 1)
        return fail("renamed directory .. is wrong");

    printf("PASS: renamed directory . and .. remain valid\n");

    if (nbfs_flush(ctx) != 0)
        return fail("flush failed");

    nbfs_close(ctx);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("reopen failed");

    if (nbfs_lookup(
            ctx, 1, "documents", &found) != 0)
        return fail("renamed directory missing after reopen");

    if (found != dir_inode)
        return fail("directory inode changed after reopen");

    if (nbfs_lookup(
            ctx, 1, "old.txt", &found) != 0)
        return fail("old.txt missing after reopen");

    if (nbfs_lookup(
            ctx, 1, "new.txt", &found) != 0)
        return fail("new.txt missing after reopen");

    printf("PASS: rename safety state survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS rename safety: OK\n");

    return 0;
}
