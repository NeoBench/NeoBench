#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vfs/vfs.h"
#include "vfs/filesystem.h"
#include "vfs/vnode.h"
#include "vfs/path.h"
#include "vfs/file.h"
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
    vfs_file_t file;
    char buffer[128];
    ssize_t n;

    printf("NeoBench NBFS VFS file read test\n");
    printf("================================\n");

    ctx = nbfs_open(TEST_IMAGE);
    if (!ctx)
        return fail("unable to open NBFS image");

    printf("PASS: NBFS image opened\n");

    if (vfs_filesystem_init(&fs, "nbfs", 4096, 1) != 0)
    {
        nbfs_close(ctx);
        return fail("filesystem init");
    }

    /*
     * The VFS filesystem currently carries the NBFS context
     * through private_data.
     */
    fs.private_data = ctx;
    fs.lookup = vfs_nbfs_lookup;

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

    if (vfs_lookup(&root_path, "docs", &docs) != 0)
    {
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("lookup /docs");
    }

    printf("PASS: /docs -> inode %llu\n",
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

    if (vfs_lookup(&docs_path, "readme.txt", &readme) != 0)
    {
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("lookup /docs/readme.txt");
    }

    printf("PASS: /docs/readme.txt -> inode %llu\n",
           (unsigned long long)readme.ino);

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

    printf("PASS: readme.txt is a regular file\n");

    if (vfs_file_init(&file, &readme, 0) != 0)
    {
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("file init");
    }


    memset(buffer, 0, sizeof(buffer));

    n = vfs_file_read(&file, buffer, sizeof(buffer) - 1);

    if (n < 0)
    {
        vfs_file_destroy(&file);
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("file read");
    }

    printf("PASS: read %zd bytes\n", n);
    printf("DATA: \"%s\"\n", buffer);

    if (strcmp(buffer, "NeoBench pathname resolution test") != 0)
    {
        vfs_file_destroy(&file);
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("file contents mismatch");
    }

    printf("PASS: file contents match\n");

    /*
     * Verify sequential EOF behavior.
     *
     * The first read consumed the entire file. A second
     * read must therefore return zero bytes.
     */
    memset(buffer, 0, sizeof(buffer));

    n = vfs_file_read(&file, buffer, sizeof(buffer) - 1);

    if (n != 0)
    {
        vfs_file_destroy(&file);
        vfs_vnode_put(&readme);
        vfs_path_destroy(&docs_path);
        vfs_vnode_put(&docs);
        vfs_path_destroy(&root_path);
        vfs_vnode_put(&root);
        vfs_filesystem_destroy(&fs);
        nbfs_close(ctx);
        return fail("EOF read did not return zero");
    }

    printf("PASS: EOF returns 0 bytes\n");

    vfs_file_destroy(&file);
    vfs_vnode_put(&readme);
    vfs_path_destroy(&docs_path);
    vfs_vnode_put(&docs);
    vfs_path_destroy(&root_path);
    vfs_vnode_put(&root);
    vfs_filesystem_destroy(&fs);
    nbfs_close(ctx);

    printf("\nNeoBench NBFS VFS file read test: OK\n");
    printf("RESULT: PASS\n");

    return 0;
}
