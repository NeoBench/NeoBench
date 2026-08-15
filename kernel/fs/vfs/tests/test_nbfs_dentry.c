#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vfs.h"
#include "filesystem.h"
#include "vnode.h"
#include "path.h"
#include "dentry.h"

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
    uint64_t inode = 0;

    vfs_filesystem_t fs;
    vfs_vnode_t root;
    vfs_vnode_t docs;
    vfs_dentry_t dentry;

    printf("NeoBench NBFS VFS dentry lookup test\n");
    printf("=====================================\n");

    /*
     * Open the verified NBFS image.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    printf("PASS: NBFS image opened\n");

    /*
     * Resolve /docs through NBFS.
     */
    if (nbfs_lookup(ctx, 1, "docs", &inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup of docs failed");
    }

    printf("PASS: NBFS lookup docs -> inode %llu\n",
           (unsigned long long)inode);

    if (inode != 2)
    {
        nbfs_close(ctx);
        return fail("docs inode is not 2");
    }

    printf("PASS: docs inode = 2\n");

    /*
     * Initialise the VFS filesystem.
     */
    if (vfs_filesystem_init(
            &fs,
            "nbfs",
            4096,
            1) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem init");
    }

    printf("PASS: VFS filesystem initialized\n");

    /*
     * Map NBFS root inode 1 into a VFS vnode.
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

    printf("PASS: NBFS root inode mapped to VFS vnode\n");

    /*
     * Map NBFS docs inode 2 into a VFS vnode.
     */
    if (vfs_vnode_init(
            &docs,
            &fs,
            inode,
            VFS_VNODE_DIR) != 0)
    {
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("docs vnode init");
    }

    printf("PASS: docs inode mapped to VFS vnode\n");

    /*
     * Create the VFS dentry:
     *
     *     root vnode
     *          |
     *       docs
     *          |
     *     docs vnode
     */
    if (vfs_dentry_init(
            &dentry,
            "docs",
            &root,
            &docs) != 0)
    {
        vfs_vnode_put(&docs);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("dentry init");
    }

    printf("PASS: VFS dentry initialized\n");

    /*
     * Verify the dentry name.
     */
    if (strcmp(dentry.name, "docs") != 0)
    {
        vfs_dentry_put(&dentry);
        vfs_vnode_put(&docs);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("dentry name is incorrect");
    }

    printf("PASS: dentry name is docs\n");

    /*
     * Verify the dentry parent.
     */
    if (dentry.parent != &root)
    {
        vfs_dentry_put(&dentry);
        vfs_vnode_put(&docs);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("dentry parent is not root vnode");
    }

    printf("PASS: dentry parent is NBFS root\n");

    /*
     * Verify the dentry target vnode.
     */
    if (dentry.vnode != &docs)
    {
        vfs_dentry_put(&dentry);
        vfs_vnode_put(&docs);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("dentry vnode is not docs vnode");
    }

    printf("PASS: dentry points to docs vnode\n");

    /*
     * A newly-created dentry owns one reference to itself.
     */
    if (dentry.refcount != 1)
    {
        vfs_dentry_put(&dentry);
        vfs_vnode_put(&docs);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("initial dentry refcount is not 1");
    }

    printf("PASS: dentry reference count = 1\n");

    /*
     * Cleanup.
     */
    vfs_dentry_put(&dentry);
    vfs_vnode_put(&docs);
    vfs_vnode_put(&root);
    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench NBFS VFS dentry lookup test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
