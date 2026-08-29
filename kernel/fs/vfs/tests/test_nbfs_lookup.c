#include <stdio.h>
#include <stdint.h>

#include "vfs.h"
#include "filesystem.h"
#include "vnode.h"
#include "path.h"
#include "vfs/nbfs_vfs.h"
#include "libnbfs.h"

#define TEST_IMAGE "../../../images/test-path-resolution.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    nbfs_context_t *ctx;
    vfs_filesystem_t fs;

    vfs_vnode_t root;
    vfs_vnode_t docs;
    vfs_vnode_t readme;

    vfs_path_t root_path;
    vfs_path_t docs_path;

    printf("NeoBench NBFS VFS lookup test\n");
    printf("=============================\n");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    printf("PASS: NBFS image opened\n");

    if (vfs_filesystem_init(
            &fs,
            "nbfs",
            4096,
            1) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem init");
    }

    /*
     * Connect the NBFS implementation to the generic VFS.
     */
    if (vfs_nbfs_bind(&fs, ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem bind");
    }

    printf("PASS: VFS filesystem initialized\n");

    /*
     * Root inode 1.
     */
    if (vfs_vnode_init(
            &root,
            &fs,
            1,
            VFS_VNODE_DIR) != 0)
    {
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("root vnode init");
    }

    printf("PASS: root vnode initialized\n");

    if (vfs_path_init(
            &root_path,
            &root) != 0)
    {
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("root path init");
    }

    printf("PASS: root path initialized\n");

    /*
     * Root directory:
     *
     * /
     * └── docs       inode 2
     */
    if (vfs_lookup(
            &root_path,
            "docs",
            &docs) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("VFS lookup /docs");
    }

    printf(
        "PASS: VFS /docs -> inode %llu\n",
        (unsigned long long)docs.ino);

    if (docs.ino != 2)
    {
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("/docs inode is not 2");
    }

    printf("PASS: /docs inode = 2\n");

    if (docs.type != VFS_VNODE_DIR)
    {
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("/docs is not a directory");
    }

    printf("PASS: /docs is a directory\n");

    /*
     * Turn /docs into a VFS path.
     */
    if (vfs_path_init(
            &docs_path,
            &docs) != 0)
    {
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("docs path init");
    }

    printf("PASS: /docs path initialized\n");

    /*
     * Nested lookup:
     *
     * /docs/readme.txt
     *
     * readme.txt = inode 3
     */
    if (vfs_lookup(
            &docs_path,
            "readme.txt",
            &readme) != 0)
    {
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("VFS lookup /docs/readme.txt");
    }

    printf(
        "PASS: VFS /docs/readme.txt -> inode %llu\n",
        (unsigned long long)readme.ino);

    if (readme.ino != 3)
    {
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("readme.txt inode is not 3");
    }

    printf("PASS: readme.txt inode = 3\n");

    if (readme.type != VFS_VNODE_REG)
    {
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("readme.txt is not a regular file");
    }

    printf("PASS: /docs/readme.txt is a regular file\n");

    /*
     * Cleanup.
     */
    vfs_vnode_put(&readme);

    vfs_path_destroy(&docs_path);
    vfs_vnode_put(&docs);

    vfs_path_destroy(&root_path);
    vfs_vnode_put(&root);

    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench NBFS VFS lookup test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
