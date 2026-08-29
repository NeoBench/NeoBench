#include <stdio.h>
#include <stdint.h>

#include "vfs/file.h"
#include "vfs/filesystem.h"
#include "vfs/path.h"
#include "vfs/vnode.h"
#include "nbfs_vfs.h"
#include "libnbfs.h"

#define TEST_IMAGE "../../../images/test-path-resolution.nbfs"

static int fail(const char *msg)
{
    printf("FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    nbfs_context_t *ctx;
    vfs_filesystem_t fs;
    vfs_vnode_t root;
    vfs_path_t path;
    vfs_file_t file;
    int64_t pos;
    ssize_t n;
    char buffer[8];

    printf("NeoBench VFS tell test\n");
    printf("======================\n");

    ctx = nbfs_open(TEST_IMAGE);
    if (!ctx)
        return fail("NBFS image opened");
    printf("PASS: NBFS image opened\n");

    if (vfs_filesystem_init(&fs, "nbfs", 512, 0) != 0)
        return fail("VFS filesystem initialized");
    if (vfs_nbfs_bind(&fs, ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem bind");
    }

    printf("PASS: VFS filesystem initialized\n");

    if (vfs_vnode_init(&root, &fs, 1, VFS_VNODE_DIR) != 0)
        return fail("root vnode initialized");
    printf("PASS: root vnode initialized\n");

    if (vfs_path_init(&path, &root) != 0)
        return fail("root path initialized");
    printf("PASS: root path initialized\n");

    if (vfs_open(&path, "/docs/readme.txt", 0, &file) != 0)
        return fail("vfs_open /docs/readme.txt");
    printf("PASS: vfs_open /docs/readme.txt\n");

    pos = vfs_file_tell(&file);
    if (pos != 0)
        return fail("initial tell");
    printf("PASS: initial tell -> 0\n");

    pos = vfs_file_seek(&file, 5, VFS_SEEK_SET);
    if (pos != 5)
        return fail("seek to 5");
    printf("PASS: seek -> 5\n");

    pos = vfs_file_tell(&file);
    if (pos != 5)
        return fail("tell after seek");
    printf("PASS: tell after seek -> 5\n");

    n = vfs_file_read(&file, buffer, 3);
    if (n != 3)
        return fail("read 3 bytes");
    printf("PASS: read 3 bytes\n");

    pos = vfs_file_tell(&file);
    if (pos != 8)
        return fail("tell after read");
    printf("PASS: tell after read -> 8\n");

    pos = vfs_file_seek(&file, -3, VFS_SEEK_CUR);
    if (pos != 5)
        return fail("seek backward");
    printf("PASS: seek backward -> 5\n");

    pos = vfs_file_tell(&file);
    if (pos != 5)
        return fail("tell after backward seek");
    printf("PASS: tell after backward seek -> 5\n");

    pos = vfs_file_seek(&file, 0, VFS_SEEK_END);
    if (pos != 33)
        return fail("seek end");
    printf("PASS: seek end -> 33\n");

    pos = vfs_file_tell(&file);
    if (pos != 33)
        return fail("tell at EOF");
    printf("PASS: tell at EOF -> 33\n");

    vfs_file_destroy(&file);
    vfs_path_destroy(&path);
    vfs_vnode_put(&root);
    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench VFS tell test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
