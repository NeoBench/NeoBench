#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-rename-safety.nbfs"

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

    while ((n = fread(
                buffer,
                1,
                sizeof(buffer),
                src)) != 0)
    {
        if (fwrite(
                buffer,
                1,
                n,
                dst) != n)
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

    const char *long_name =
        "this-is-a-much-longer-name.txt";

    printf("NBFS rename safety test\n");
    printf("=======================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * ---------------------------------------------------------
     * Existing-name collision.
     * ---------------------------------------------------------
     */

    if (nbfs_create_file(
            ctx,
            1,
            "old.txt") != 0)
        return fail("create old.txt failed");

    if (nbfs_create_file(
            ctx,
            1,
            "new.txt") != 0)
        return fail("create new.txt failed");

    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &old_inode) != 0)
        return fail("lookup old.txt failed");

    if (nbfs_lookup(
            ctx,
            1,
            "new.txt",
            &new_inode) != 0)
        return fail("lookup new.txt failed");

    if (nbfs_rename(
            ctx,
            1,
            "old.txt",
            "new.txt") == 0)
        return fail(
            "rename over existing name succeeded");

    printf(
        "PASS: rename over existing name rejected\n");

    /*
     * Both original entries must remain intact.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &found) != 0)
        return fail(
            "old.txt disappeared after rejected rename");

    if (found != old_inode)
        return fail(
            "old.txt inode changed");

    if (nbfs_lookup(
            ctx,
            1,
            "new.txt",
            &found) != 0)
        return fail(
            "new.txt disappeared after rejected rename");

    if (found != new_inode)
        return fail(
            "new.txt inode changed");

    printf(
        "PASS: collision left both files intact\n");

    /*
     * ---------------------------------------------------------
     * Longer name.
     * ---------------------------------------------------------
     */

    if (nbfs_rename(
            ctx,
            1,
            "old.txt",
            long_name) != 0)
        return fail(
            "rename to longer name failed");

    printf(
        "PASS: rename to longer name succeeded\n");

    /*
     * Old name must disappear.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &found) == 0)
        return fail(
            "old.txt still exists after longer rename");

    /*
     * New name must point to the same inode.
     */
    if (nbfs_lookup(
            ctx,
            1,
            long_name,
            &found) != 0)
        return fail(
            "longer renamed file not found");

    if (found != old_inode)
        return fail(
            "longer rename changed inode");

    printf(
        "PASS: longer rename preserved inode\n");

    /*
     * ---------------------------------------------------------
     * Directory rename.
     * ---------------------------------------------------------
     */

    if (nbfs_create_directory(
            ctx,
            1,
            "docs") != 0)
        return fail(
            "create docs failed");

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &dir_inode) != 0)
        return fail(
            "lookup docs failed");

    if (nbfs_rename(
            ctx,
            1,
            "docs",
            "documents") != 0)
        return fail(
            "directory rename failed");

    printf(
        "PASS: directory rename succeeded\n");

    /*
     * Old directory name must disappear.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) == 0)
        return fail(
            "old directory name still exists");

    /*
     * New directory name must exist.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "documents",
            &found) != 0)
        return fail(
            "new directory name missing");

    if (found != dir_inode)
        return fail(
            "directory inode changed");

    /*
     * "." must still point to itself.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            ".",
            &found) != 0)
        return fail(
            "renamed directory . failed");

    if (found != dir_inode)
        return fail(
            "renamed directory . is wrong");

    /*
     * ".." must still point to root.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            "..",
            &found) != 0)
        return fail(
            "renamed directory .. failed");

    if (found != 1)
        return fail(
            "renamed directory .. is wrong");

    printf(
        "PASS: renamed directory . and .. remain valid\n");

    /*
     * ---------------------------------------------------------
     * Persistence.
     * ---------------------------------------------------------
     */

    if (nbfs_flush(ctx) != 0)
        return fail("flush failed");

    nbfs_close(ctx);

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("reopen failed");

    if (nbfs_lookup(
            ctx,
            1,
            "documents",
            &found) != 0)
        return fail(
            "renamed directory missing after reopen");

    if (found != dir_inode)
        return fail(
            "directory inode changed after reopen");

    if (nbfs_lookup(
            ctx,
            1,
            "old.txt",
            &found) == 0)
        return fail(
            "old.txt returned after reopen");

    if (nbfs_lookup(
            ctx,
            1,
            long_name,
            &found) != 0)
        return fail(
            "long renamed file missing after reopen");

    if (found != old_inode)
        return fail(
            "long renamed file inode changed");

    if (nbfs_lookup(
            ctx,
            1,
            "new.txt",
            &found) != 0)
        return fail(
            "new.txt missing after reopen");

    if (found != new_inode)
        return fail(
            "new.txt inode changed after reopen");

    printf(
        "PASS: rename safety state survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS rename safety: OK\n");

    return 0;
}
