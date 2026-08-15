#include <stddef.h>
#include <string.h>

#include "vfs/vfs.h"
#include "console.h"

#define VFS_MAX_FILESYSTEMS 8

static int vfs_ready;
static vfs_filesystem_t *vfs_filesystems[VFS_MAX_FILESYSTEMS];

int vfs_init(void)
{
    if (vfs_ready)
        return 0;

    memset(vfs_filesystems, 0, sizeof(vfs_filesystems));

    console_write("VFS:       initializing... ");

    vfs_ready = 1;

    console_write("OK\n");

    return 0;
}

void vfs_shutdown(void)
{
    memset(vfs_filesystems, 0, sizeof(vfs_filesystems));
    vfs_ready = 0;
}

int vfs_register_filesystem(vfs_filesystem_t *fs)
{
    int i;

    if (!vfs_ready || !fs || !fs->name)
        return -1;

    for (i = 0; i < VFS_MAX_FILESYSTEMS; i++)
    {
        if (vfs_filesystems[i] == fs)
            return 0;
    }

    for (i = 0; i < VFS_MAX_FILESYSTEMS; i++)
    {
        if (!vfs_filesystems[i])
        {
            vfs_filesystems[i] = fs;
            return 0;
        }
    }

    return -1;
}

int vfs_unregister_filesystem(vfs_filesystem_t *fs)
{
    int i;

    if (!fs)
        return -1;

    for (i = 0; i < VFS_MAX_FILESYSTEMS; i++)
    {
        if (vfs_filesystems[i] == fs)
        {
            vfs_filesystems[i] = NULL;
            return 0;
        }
    }

    return -1;
}

vfs_filesystem_t *vfs_find_filesystem(const char *name)
{
    int i;

    if (!vfs_ready || !name)
        return NULL;

    for (i = 0; i < VFS_MAX_FILESYSTEMS; i++)
    {
        if (vfs_filesystems[i] &&
            strcmp(vfs_filesystems[i]->name, name) == 0)
        {
            return vfs_filesystems[i];
        }
    }

    return NULL;
}
