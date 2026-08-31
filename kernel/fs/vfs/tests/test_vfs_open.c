#include "nbfs_vfs.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vfs.h"
#include "filesystem.h"
#include "vnode.h"
#include "path.h"
#include "file.h"
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
    vfs_path_t root_path;
    vfs_file_t file;
    char buffer[128];
    ssize_t n;

    printf("NeoBench VFS open test\n");
    printf("======================\n");

    ctx = nbfs_open(TEST_IMAGE);
    if (!ctx)
        return fail("unable to open NBFS image");

    printf("PASS: NBFS image opened\n");

    if (vfs_filesystem_init(&fs, "nbfs", 4096, 1) != 0)
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

    if (vfs_vnode_init(&root, &fs, 1, VFS_VNODE_DIR) != 0)
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

    if (vfs_open(
            &root_path,
            "/docs/readme.txt",
            0,
            &file) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("vfs_open /docs/readme.txt");
    }

    printf("PASS: vfs_open /docs/readme.txt\n");

    if (!file.vnode)
    {
        vfs_file_destroy(&file);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("opened file has no vnode");
    }

    printf("PASS: opened file has vnode\n");

    if (file.vnode->ino != 3)
    {
        vfs_file_destroy(&file);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("opened file has wrong inode");
    }

    printf("PASS: opened vnode inode = %llu\n",
           (unsigned long long)file.vnode->ino);

    if (file.vnode->type != VFS_VNODE_REG)
    {
        vfs_file_destroy(&file);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("opened vnode is not regular file");
    }

    printf("PASS: opened vnode is regular file\n");

    memset(buffer, 0, sizeof(buffer));

    n = vfs_file_read(&file, buffer, sizeof(buffer) - 1);

    if (n != 33)
    {
        vfs_file_destroy(&file);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("unexpected read length");
    }

    printf("PASS: read %zd bytes\n", n);
    printf("DATA: \"%s\"\n", buffer);

    if (strcmp(buffer, "NeoBench pathname resolution test") != 0)
    {
        vfs_file_destroy(&file);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("file contents mismatch");
    }

    printf("PASS: file contents match\n");

    vfs_file_destroy(&file);
    vfs_path_destroy(&root_path);
    vfs_vnode_put(&root);
    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench VFS open test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
