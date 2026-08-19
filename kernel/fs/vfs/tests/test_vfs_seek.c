#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vfs/file.h"
#include "vfs/filesystem.h"
#include "vfs/path.h"
#include "vfs/vnode.h"
#include "nbfs_vfs.h"
#include "libnbfs.h"

int main(void)
{
    nbfs_context_t *ctx;
    vfs_filesystem_t fs;
    vfs_vnode_t root;
    vfs_path_t path;
    vfs_file_t file;
    char buffer[128];
    ssize_t n;
    int64_t pos;

    printf("NeoBench VFS seek test\n");
    printf("======================\n");

    ctx = nbfs_open("../../../images/test-path-resolution.nbfs");
    if (!ctx)
    {
        printf("FAIL: NBFS image opened\n");
        return 1;
    }
    printf("PASS: NBFS image opened\n");

    if (vfs_filesystem_init(&fs, "nbfs", 512, 0) != 0)
    {
        printf("FAIL: VFS filesystem initialized\n");
        nbfs_close(ctx);
        return 1;
    }
    fs.private_data = ctx;
    fs.lookup = vfs_nbfs_lookup;
    printf("PASS: VFS filesystem initialized\n");

    if (vfs_vnode_init(&root, &fs, 1, VFS_VNODE_DIR) != 0)
    {
        printf("FAIL: root vnode initialized\n");
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: root vnode initialized\n");

    if (vfs_path_init(&path, &root) != 0)
    {
        printf("FAIL: root path initialized\n");
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: root path initialized\n");

    if (vfs_open(&path, "/docs/readme.txt", 0, &file) != 0)
    {
        printf("FAIL: vfs_open /docs/readme.txt\n");
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: vfs_open /docs/readme.txt\n");

    pos = vfs_file_seek(&file, 5, VFS_SEEK_SET);
    if (pos != 5 || file.offset != 5)
    {
        printf("FAIL: SEEK_SET\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: SEEK_SET -> 5\n");

    pos = vfs_file_seek(&file, 7, VFS_SEEK_CUR);
    if (pos != 12 || file.offset != 12)
    {
        printf("FAIL: SEEK_CUR\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: SEEK_CUR -> 12\n");

    pos = vfs_file_seek(&file, -2, VFS_SEEK_CUR);
    if (pos != 10 || file.offset != 10)
    {
        printf("FAIL: negative SEEK_CUR\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: negative SEEK_CUR -> 10\n");

    pos = vfs_file_seek(&file, -1, VFS_SEEK_SET);
    if (pos != -1 || file.offset != 10)
    {
        printf("FAIL: negative SEEK_SET rejection\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: negative SEEK_SET rejected\n");

    pos = vfs_file_seek(&file, 0, VFS_SEEK_END);
    if (pos != 33 || file.offset != 33)
    {
        printf("FAIL: SEEK_END\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }
    printf("PASS: SEEK_END -> 33\n");

    memset(buffer, 0, sizeof(buffer));

    pos = vfs_file_seek(&file, 0, VFS_SEEK_SET);
    if (pos != 0)
    {
        printf("FAIL: rewind\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }

    n = vfs_file_read(&file, buffer, 5);
    if (n != 5)
    {
        printf("FAIL: read after seek\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }

    if (memcmp(buffer, "NeoBe", 5) != 0)
    {
        printf("FAIL: data after seek\n");
        vfs_file_destroy(&file);
        vfs_path_destroy(&path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return 1;
    }

    printf("PASS: read after seek\n");

    vfs_file_destroy(&file);
    vfs_path_destroy(&path);
    vfs_vnode_put(&root);
    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench VFS seek test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
