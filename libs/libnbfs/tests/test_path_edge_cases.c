#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-path-edge-cases.nbfs"

static int copy_image(const char *source, const char *destination)
{
    FILE *src = fopen(source, "rb");
    FILE *dst;
    unsigned char buffer[65536];
    size_t n;

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

static int expect_path(
    nbfs_context_t *ctx,
    uint64_t start,
    const char *path,
    uint64_t expected)
{
    uint64_t inode = 0;

    if (nbfs_resolve_path(
            ctx,
            start,
            path,
            &inode) != 0)
    {
        printf("FAIL: \"%s\" rejected\n", path);
        return 1;
    }

    if (inode != expected)
    {
        printf(
            "FAIL: \"%s\" -> %llu, expected %llu\n",
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

static int expect_fail(
    nbfs_context_t *ctx,
    uint64_t start,
    const char *path)
{
    uint64_t inode = 0;

    if (nbfs_resolve_path(
            ctx,
            start,
            path,
            &inode) == 0)
    {
        printf(
            "FAIL: \"%s\" unexpectedly resolved to inode %llu\n",
            path,
            (unsigned long long)inode);

        return 1;
    }

    printf("PASS: \"%s\" correctly rejected\n", path);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t docs = 0;
    uint64_t readme = 0;
    uint64_t subdir = 0;
    uint64_t nested = 0;
    int failed = 0;

    printf("NBFS pathname edge-case test\n");
    printf("============================\n");

    if (copy_image(SOURCE_IMAGE, TEST_IMAGE) != 0)
    {
        printf("FAIL: could not copy verified image\n");
        return 1;
    }

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
    {
        printf("FAIL: could not open test image\n");
        return 1;
    }

    /*
     * Build:
     *
     * /
     * └── docs
     *     ├── readme.txt
     *     └── subdir
     *         └── file.txt
     */

    if (nbfs_create_directory(ctx, 1, "docs") != 0)
    {
        printf("FAIL: could not create docs\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_lookup(ctx, 1, "docs", &docs) != 0)
    {
        printf("FAIL: could not lookup docs\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_create_file(ctx, docs, "readme.txt") != 0)
    {
        printf("FAIL: could not create readme.txt\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_lookup(ctx, docs, "readme.txt", &readme) != 0)
    {
        printf("FAIL: could not lookup readme.txt\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_create_directory(ctx, docs, "subdir") != 0)
    {
        printf("FAIL: could not create subdir\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_lookup(ctx, docs, "subdir", &subdir) != 0)
    {
        printf("FAIL: could not lookup subdir\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_create_file(ctx, subdir, "file.txt") != 0)
    {
        printf("FAIL: could not create file.txt\n");
        nbfs_close(ctx);
        return 1;
    }

    if (nbfs_lookup(ctx, subdir, "file.txt", &nested) != 0)
    {
        printf("FAIL: could not lookup file.txt\n");
        nbfs_close(ctx);
        return 1;
    }

    printf("\nBasic normalization\n");
    printf("-------------------\n");

    failed |= expect_path(ctx, 1, "/", 1);
    failed |= expect_path(ctx, 1, "///", 1);
    failed |= expect_path(ctx, 1, "/docs", docs);
    failed |= expect_path(ctx, 1, "//docs", docs);
    failed |= expect_path(ctx, 1, "///docs///", docs);

    printf("\nDot components\n");
    printf("--------------\n");

    failed |= expect_path(ctx, docs, ".", docs);
    failed |= expect_path(ctx, docs, "./", docs);
    failed |= expect_path(ctx, docs, "././", docs);
    failed |= expect_path(ctx, docs, "./readme.txt", readme);
    failed |= expect_path(ctx, subdir, ".", subdir);
    failed |= expect_path(ctx, subdir, "./file.txt", nested);

    printf("\nParent components\n");
    printf("-----------------\n");

    failed |= expect_path(ctx, docs, "..", 1);
    failed |= expect_path(ctx, docs, "../", 1);
    failed |= expect_path(ctx, docs, "../docs", docs);
    failed |= expect_path(ctx, docs, "../docs/readme.txt", readme);

    failed |= expect_path(ctx, subdir, "..", docs);
    failed |= expect_path(ctx, subdir, "../readme.txt", readme);
    failed |= expect_path(ctx, subdir, "../../", 1);
    failed |= expect_path(ctx, subdir, "../../docs", docs);

    printf("\nMixed normalization\n");
    printf("-------------------\n");

    failed |= expect_path(
        ctx,
        1,
        "/docs/./readme.txt",
        readme);

    failed |= expect_path(
        ctx,
        1,
        "/docs/../docs/readme.txt",
        readme);

    failed |= expect_path(
        ctx,
        1,
        "/docs//subdir///file.txt",
        nested);

    failed |= expect_path(
        ctx,
        docs,
        "./subdir/../readme.txt",
        readme);

    failed |= expect_path(
        ctx,
        subdir,
        "./.././../docs/readme.txt",
        readme);

    printf("\nRoot parent handling\n");
    printf("--------------------\n");

    failed |= expect_path(ctx, 1, "..", 1);
    failed |= expect_path(ctx, 1, "../..", 1);
    failed |= expect_path(ctx, 1, "../../docs", docs);

    printf("\nInvalid paths\n");
    printf("-------------\n");

    failed |= expect_fail(ctx, 1, "");
    failed |= expect_fail(ctx, 1, "missing");
    failed |= expect_fail(ctx, 1, "/missing");
    failed |= expect_fail(ctx, 1, "missing/child");

    /*
     * A regular file must not behave like a directory.
     */
    failed |= expect_fail(ctx, readme, "child");
    failed |= expect_fail(ctx, readme, ".");
    failed |= expect_fail(ctx, readme, "..");

    nbfs_close(ctx);

    if (failed)
    {
        printf("\nNBFS pathname edge cases: FAIL\n");
        return 1;
    }

    printf("\nNBFS pathname edge cases: OK\n");
    return 0;
}
