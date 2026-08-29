/*
 * mkroot
 *
 * Populates a freshly formatted NBFS image with the NeoBench
 * alpha root filesystem contents.  Used to build the small root
 * image embedded into the kernel for the boot path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libnbfs.h"

#include "mkroot.h"

static const char ETC_MOTD[] =
    "Welcome to NeoBench alpha.\n"
    "The root filesystem is backed by NBFS.\n";

static const char ETC_VERSION[] =
    "NeoBench alpha\n"
    "Kernel r1.0\n"
    "Filesystem NBFS v1\n";

static const char README_TXT[] =
    "NeoBench alpha\n"
    "===============\n"
    "\n"
    "A from-scratch 68060 operating system booting an NBFS\n"
    "root filesystem through VFS.\n"
    "\n"
    "Try  nbroot   root mount status\n"
    "     nbcat /etc/motd\n"
    "     nbcat /docs/readme.txt\n";

static const char DOCS_README[] =
    "NBFS root filesystem integration complete.\n"
    "Superblock, inode table, directory lookup and extent\n"
    "reads are serviced by the kernel over a memory backed\n"
    "block device.\n";

static nbfs_context_t *ctx;

static int step(const char *what)
{
    printf("  %-44s", what);
    return 0;
}

static int fail(const char *what)
{
    printf("FAIL\n");
    fprintf(stderr, "mkroot: %s failed\n", what);
    return -1;
}

static int ok(const char *what)
{
    (void)what;
    printf("ok\n");
    return 0;
}

static int write_static_file(
    uint64_t parent_inode,
    const char *name,
    const char *content,
    size_t content_len)
{
    uint64_t inode = 0;

    step("create file");

    if (nbfs_create_file(ctx, parent_inode, name) != 0)
        return fail("create_file");

    if (nbfs_lookup(ctx, parent_inode, name, &inode) != 0)
        return fail("lookup");

    if (nbfs_write_file(ctx, inode, content, content_len) != 0)
        return fail("write_file");

    return 0;
}

int mkroot(const char *image)
{
    uint64_t root = 1;
    uint64_t etc = 0;
    uint64_t docs = 0;

    printf("NeoBench root image population\n");
    printf("===============================\n");

    printf("  opening %s\n", image);

    ctx = nbfs_open(image);

    if (!ctx)
    {
        fprintf(stderr, "mkroot: unable to open NBFS image\n");
        return 1;
    }

    /* etc/ */
    step("create dir etc");
    if (nbfs_create_directory(ctx, root, "etc") != 0)
        return fail("create etc");

    if (nbfs_lookup(ctx, root, "etc", &etc) != 0)
        return fail("lookup etc");

    ok("dir etc");

    if (write_static_file(etc, "motd", ETC_MOTD, sizeof(ETC_MOTD) - 1) != 0)
        return 1;

    ok("etc/motd");

    if (write_static_file(etc, "version", ETC_VERSION, sizeof(ETC_VERSION) - 1) != 0)
        return 1;

    ok("etc/version");

    /* docs/ */
    step("create dir docs");
    if (nbfs_create_directory(ctx, root, "docs") != 0)
        return fail("create docs");

    if (nbfs_lookup(ctx, root, "docs", &docs) != 0)
        return fail("lookup docs");

    ok("dir docs");

    if (write_static_file(docs, "readme.txt", DOCS_README, sizeof(DOCS_README) - 1) != 0)
        return 1;

    ok("docs/readme.txt");

    /* /README.txt */
    if (write_static_file(root, "README.txt", README_TXT, sizeof(README_TXT) - 1) != 0)
        return 1;

    ok("README.txt");

    step("flush image");
    if (nbfs_flush(ctx) != 0)
        return fail("flush");

    ok("flush");

    nbfs_close(ctx);

    printf("Root image population complete.\n");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: mkroot <image.nbfs>\n");
        return 1;
    }

    return mkroot(argv[1]) == 0 ? 0 : 1;
}