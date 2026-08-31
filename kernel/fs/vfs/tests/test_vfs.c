#include <stdio.h>

#include "vfs/vfs.h"
#include "vfs/filesystem.h"
#include "vfs/vnode.h"
#include "vfs/mount.h"
#include "vfs/path.h"
#include "vfs/dentry.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    vfs_filesystem_t fs;
    vfs_vnode_t root;
    vfs_mount_t mount;
    vfs_path_t path;
    vfs_dentry_t dentry;

    printf("NeoBench VFS core test\n");
    printf("======================\n");

    if (vfs_init() != 0)
        return fail("vfs_init");

    printf("PASS: VFS initialized\n");

    if (vfs_filesystem_init(
            &fs,
            "NBFS",
            4096,
            1) != 0)
        return fail("filesystem init");

    printf("PASS: filesystem initialized\n");

    if (vfs_vnode_init(
            &root,
            &fs,
            1,
            VFS_VNODE_DIR) != 0)
        return fail("root vnode init");

    printf("PASS: root vnode initialized\n");

    if (root.ino != 1)
        return fail("root inode number");

    printf("PASS: root inode = %llu\n",
           (unsigned long long)root.ino);

    if (root.type != VFS_VNODE_DIR)
        return fail("root vnode type");

    printf("PASS: root vnode is directory\n");

    if (vfs_mount_init(
            &mount,
            &fs,
            &root) != 0)
        return fail("mount init");

    printf("PASS: filesystem mounted\n");

    if (!mount.mounted)
        return fail("mount state");

    printf("PASS: mount state active\n");

    if (mount.root != &root)
        return fail("mount root vnode");

    printf("PASS: mount root vnode\n");

    if (vfs_path_init(
            &path,
            &root) != 0)
        return fail("path init");

    printf("PASS: path initialized\n");

    if (path.vnode != &root)
        return fail("path vnode");

    printf("PASS: path points to root vnode\n");

    if (root.refcount != 2)
        return fail("root vnode reference count");

    printf("PASS: root vnode refcount = %u\n",
           root.refcount);

    if (vfs_dentry_init(
            &dentry,
            "root",
            &root,
            &root) != 0)
        return fail("dentry init");

    printf("PASS: dentry initialized\n");

    if (dentry.refcount != 1)
        return fail("dentry reference count");

    printf("PASS: dentry refcount = %u\n",
           dentry.refcount);

    vfs_dentry_put(&dentry);
    vfs_path_destroy(&path);

    if (root.refcount != 1)
        return fail("root vnode reference cleanup");

    printf("PASS: vnode references released\n");

    vfs_mount_destroy(&mount);

    if (mount.mounted)
        return fail("mount destroy");

    printf("PASS: filesystem unmounted\n");

    vfs_filesystem_destroy(&fs);

    printf("PASS: filesystem destroyed\n");

    vfs_shutdown();

    printf("PASS: VFS shutdown\n");

    printf("\nNeoBench VFS core test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
