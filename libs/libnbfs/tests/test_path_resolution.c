/*
 * test_path_resolution.c
 *
 * NBFS pathname resolution test.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-path-resolution.nbfs"

static int fail(const char *message)
{
    printf("FAIL: %s\n", message);
    return 1;
}

static int copy_image(const char *source, const char *destination)
{
    FILE *src;
    FILE *dst;
    unsigned char buffer[65536];
    size_t n;

    src = fopen(source, "rb");
    if (!src)
        return -1;

    dst = fopen(destination, "wb");
    if (!dst)
    {
        fclose(src);
        return -1;
    }

    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, n, dst) != n)
        {
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    if (ferror(src))
    {
        fclose(src);
        fclose(dst);
        return -1;
    }

    fclose(src);
    fclose(dst);

    return 0;
}

static int check_path(
    nbfs_context_t *ctx,
    uint64_t start_inode,
    const char *path,
    uint64_t expected)
{
    uint64_t inode = 0;
    int rc;

    rc = nbfs_resolve_path(
        ctx,
        start_inode,
        path,
        &inode);

    if (rc != 0)
    {
        printf(
            "FAIL: resolve \"%s\" returned %d\n",
            path,
            rc);

        return 1;
    }

    if (inode != expected)
    {
        printf(
            "FAIL: \"%s\" -> inode %llu, expected %llu\n",
            path,
            (unsigned long long)inode,
            (unsigned long long)expected);

        return 1;
    }

    printf(
        "PASS: \"%s\" -> inode %llu\n",
        path,
        (unsigned long long)inode);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;

    uint64_t docs = 0;
    uint64_t subdir = 0;
    uint64_t readme = 0;
    uint64_t nested_file = 0;

    int rc;

    const char *readme_data =
        "NeoBench pathname resolution test";

    const char *nested_data =
        "Nested pathname works";

    printf("NBFS pathname resolution test\n");
    printf("=============================\n");

    /*
     * Start from a known-good NBFS image.
     *
     * Do NOT call nbfs_create() here. That creates a new image
     * file, but this test needs an already-formatted filesystem.
     */
    printf(
        "DEBUG: copying verified image: %s -> %s\n",
        SOURCE_IMAGE,
        TEST_IMAGE);

    if (copy_image(SOURCE_IMAGE, TEST_IMAGE) != 0)
        return fail("could not copy source image");

    printf("DEBUG: test image copied successfully\n");

    /*
     * Open the copied NBFS image.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("could not open test image");

    printf("DEBUG: test image opened successfully\n");

    /*
     * Create:
     *
     * /
     * └── docs
     *     ├── readme.txt
     *     └── subdir
     *         └── file.txt
     */

    printf("DEBUG: creating /docs under inode 1\n");

    rc = nbfs_create_directory(
        ctx,
        1,
        "docs");

    printf(
        "DEBUG: nbfs_create_directory(1, \"docs\") = %d\n",
        rc);

    if (rc != 0)
    {
        nbfs_close(ctx);
        return fail("could not create docs");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &docs) != 0)
    {
        nbfs_close(ctx);
        return fail("could not lookup docs");
    }

    printf(
        "PASS: created docs -> inode %llu\n",
        (unsigned long long)docs);

    /*
     * Create docs/readme.txt
     */
    rc = nbfs_create_file(
        ctx,
        docs,
        "readme.txt");

    printf(
        "DEBUG: nbfs_create_file(docs, \"readme.txt\") = %d\n",
        rc);

    if (rc != 0)
    {
        nbfs_close(ctx);
        return fail("could not create readme.txt");
    }

    if (nbfs_lookup(
            ctx,
            docs,
            "readme.txt",
            &readme) != 0)
    {
        nbfs_close(ctx);
        return fail("could not lookup readme.txt");
    }

    printf(
        "PASS: created docs/readme.txt -> inode %llu\n",
        (unsigned long long)readme);

    /*
     * Write readme.txt.
     */
    if (nbfs_write_file(
            ctx,
            readme,
            readme_data,
            strlen(readme_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("could not write readme.txt");
    }

    printf("PASS: wrote docs/readme.txt\n");

    /*
     * Create docs/subdir.
     */
    printf(
        "DEBUG: creating docs/subdir under inode %llu\n",
        (unsigned long long)docs);

    rc = nbfs_create_directory(
        ctx,
        docs,
        "subdir");

    printf(
        "DEBUG: nbfs_create_directory(docs, \"subdir\") = %d\n",
        rc);

    if (rc != 0)
    {
        nbfs_close(ctx);
        return fail("could not create subdir");
    }

    if (nbfs_lookup(
            ctx,
            docs,
            "subdir",
            &subdir) != 0)
    {
        nbfs_close(ctx);
        return fail("could not lookup subdir");
    }

    printf(
        "PASS: created docs/subdir -> inode %llu\n",
        (unsigned long long)subdir);

    /*
     * Create docs/subdir/file.txt.
     */
    rc = nbfs_create_file(
        ctx,
        subdir,
        "file.txt");

    printf(
        "DEBUG: nbfs_create_file(subdir, \"file.txt\") = %d\n",
        rc);

    if (rc != 0)
    {
        nbfs_close(ctx);
        return fail("could not create nested file");
    }

    if (nbfs_lookup(
            ctx,
            subdir,
            "file.txt",
            &nested_file) != 0)
    {
        nbfs_close(ctx);
        return fail("could not lookup nested file");
    }

    printf(
        "PASS: created docs/subdir/file.txt -> inode %llu\n",
        (unsigned long long)nested_file);

    /*
     * Write nested file.
     */
    if (nbfs_write_file(
            ctx,
            nested_file,
            nested_data,
            strlen(nested_data)) != 0)
    {
        nbfs_close(ctx);
        return fail("could not write nested file");
    }

    printf("PASS: wrote docs/subdir/file.txt\n");

    /*
     * Path resolution tests.
     */
    printf("\nPath resolution:\n");

    if (check_path(
            ctx,
            1,
            "/",
            1) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs",
            docs) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/readme.txt",
            readme) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/subdir",
            subdir) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/subdir/file.txt",
            nested_file) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    /*
     * Relative path tests.
     */
    if (check_path(
            ctx,
            docs,
            "readme.txt",
            readme) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            docs,
            "subdir",
            subdir) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            docs,
            "subdir/file.txt",
            nested_file) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    /*
     * Dot and dot-dot resolution.
     */
    if (check_path(
            ctx,
            docs,
            ".",
            docs) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            docs,
            "..",
            1) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            subdir,
            ".",
            subdir) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            subdir,
            "..",
            docs) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    /*
     * Flush and reopen to make sure pathname resolution survives
     * persistence.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("could not flush filesystem");
    }

    nbfs_close(ctx);

    printf("\nPASS: filesystem closed cleanly\n");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("could not reopen test image");

    printf("PASS: filesystem reopened successfully\n");

    /*
     * Verify the important paths after reopening.
     */
    if (check_path(
            ctx,
            1,
            "/docs",
            docs) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/readme.txt",
            readme) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/subdir",
            subdir) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_path(
            ctx,
            1,
            "/docs/subdir/file.txt",
            nested_file) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    nbfs_close(ctx);

    printf("\nNBFS pathname resolution: OK\n");
    printf("PASS\n");

    return 0;
}
