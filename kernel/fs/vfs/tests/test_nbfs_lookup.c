#include <stdio.h>
#include <stdint.h>

#include "vfs.h"
#include "filesystem.h"
#include "vnode.h"
#include "path.h"

#include "libnbfs.h"

#define TEST_IMAGE "../../../../images/test-path-resolution.nbfs"

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
    vfs_vnode_t integration;
    vfs_vnode_t hello;
    vfs_path_t root_path;
    vfs_path_t integration_path;

    printf("NeoBench NBFS VFS nested lookup test\n");
    printf("====================================\n");

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
     * The VFS filesystem currently carries the NBFS context
     * through private_data.
     */
    fs.private_data = ctx;

    printf("PASS: VFS filesystem initialized\n");

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

    if (vfs_path_init(&root_path, &root) != 0)
    {
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("root path init");
    }

    printf("PASS: root path initialized\n");

    /*
     * VFS lookup:
     *
     *     / -> integration
     */
    if (vfs_lookup(
            &root_path,
            "integration",
            &integration) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("VFS lookup /integration");
    }

    printf(
        "PASS: VFS /integration -> inode %llu\n",
        (unsigned long long)integration.ino);

    if (integration.type != VFS_VNODE_DIR)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&integration);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("/integration is not VFS_VNODE_DIR");
    }

    printf("PASS: /integration is a directory\n");

    /*
     * Turn /integration into a VFS path.
     */
    if (vfs_path_init(
            &integration_path,
            &integration) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&integration);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("integration path init");
    }

    /*
     * Nested VFS lookup:
     *
     *     /integration -> hello.txt
     */
    if (vfs_lookup(
            &integration_path,
            "hello.txt",
            &hello) != 0)
    {
        vfs_path_destroy(&integration_path);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&integration);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("VFS lookup /integration/hello.txt");
    }

    printf(
        "PASS: VFS /integration/hello.txt -> inode %llu\n",
        (unsigned long long)hello.ino);

    if (hello.type != VFS_VNODE_REG)
    {
        vfs_vnode_put(&hello);
        vfs_path_destroy(&integration_path);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&integration);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("hello.txt is not VFS_VNODE_REG");
    }

    printf("PASS: /integration/hello.txt is a regular file\n");

    vfs_vnode_put(&hello);
    vfs_path_destroy(&integration_path);
    vfs_path_destroy(&root_path);
    vfs_vnode_put(&integration);
    vfs_vnode_put(&root);

    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench NBFS VFS nested lookup test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
