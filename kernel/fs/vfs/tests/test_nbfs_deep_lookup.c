#include <stdio.h>
#include <stdint.h>

#include "vfs/vfs.h"
#include "vfs/filesystem.h"
#include "vfs/vnode.h"
#include "vfs/path.h"
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
    vfs_vnode_t subdir;
    vfs_vnode_t file;

    vfs_path_t root_path;
    vfs_path_t docs_path;
    vfs_path_t subdir_path;

    printf("NeoBench NBFS VFS deep lookup test\n");
    printf("==================================\n");

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
    if (vfs_nbfs_bind(&fs, ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem bind");
    }

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

    if (vfs_lookup(
            &root_path,
            "docs",
            &docs) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("lookup /docs");
    }

    printf(
        "PASS: /docs -> inode %llu\n",
        (unsigned long long)docs.ino);

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

    if (vfs_path_init(&docs_path, &docs) != 0)
    {
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("docs path init");
    }

    if (vfs_lookup(
            &docs_path,
            "subdir",
            &subdir) != 0)
    {
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("lookup /docs/subdir");
    }

    printf(
        "PASS: /docs/subdir -> inode %llu\n",
        (unsigned long long)subdir.ino);

    if (subdir.type != VFS_VNODE_DIR)
    {
        vfs_vnode_put(&subdir);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("/docs/subdir is not a directory");
    }

    printf("PASS: /docs/subdir is a directory\n");

    if (vfs_path_init(&subdir_path, &subdir) != 0)
    {
        vfs_vnode_put(&subdir);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("subdir path init");
    }

    if (vfs_lookup(
            &subdir_path,
            "file.txt",
            &file) != 0)
    {
        vfs_path_destroy(&subdir_path);
        vfs_vnode_put(&subdir);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("lookup /docs/subdir/file.txt");
    }

    printf(
        "PASS: /docs/subdir/file.txt -> inode %llu\n",
        (unsigned long long)file.ino);

    if (file.type != VFS_VNODE_REG)
    {
        vfs_vnode_put(&file);
        vfs_path_destroy(&subdir_path);
        vfs_vnode_put(&subdir);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("file.txt is not a regular file");
    }

    printf("PASS: /docs/subdir/file.txt is a regular file\n");

    vfs_vnode_put(&file);
    vfs_path_destroy(&subdir_path);
    vfs_vnode_put(&subdir);
    vfs_path_destroy(&docs_path);
    vfs_vnode_put(&docs);
    vfs_path_destroy(&root_path);
    vfs_vnode_put(&root);

    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench NBFS VFS deep lookup test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
