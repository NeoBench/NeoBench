#include <stddef.h>
#include <string.h>

#include "vfs/vfs.h"
#include "vfs/mount.h"
#include "vfs/vnode.h"
#include "block/device.h"
#include "nbfs.h"
#include "console.h"

#define VFS_MAX_FILESYSTEMS 8

static int vfs_ready;
static vfs_filesystem_t *vfs_filesystems[VFS_MAX_FILESYSTEMS];

static vfs_filesystem_t root_fs;
static vfs_mount_t root_mount;
static vfs_vnode_t root_vnode;

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

/*
 * Mount a filesystem instance as the VFS root "/".
 *
 * The provided block device is attached to the NBFS kernel driver and
 * becomes the system root.  A root vnode is prepared so pathname
 * resolution can start from "/".
 */
int vfs_mount_root(
    const char *fsname,
    block_device_t *device)
{
    uint64_t root_inode;

    if (!vfs_ready || !fsname || !device)
        return -1;

    if (vfs_filesystem_init(
            &root_fs,
            fsname,
            device->block_size,
            0) != 0)
        return -1;

    if (nbfs_kernel_mount(
            &root_fs,
            device) != 0)
    {
        vfs_filesystem_destroy(&root_fs);
        return -1;
    }

    if (vfs_register_filesystem(&root_fs) != 0)
    {
        vfs_filesystem_destroy(&root_fs);
        return -1;
    }

    if (nbfs_kernel_root_inode(
            &root_fs,
            &root_inode) != 0)
        return -1;

    if (vfs_vnode_init(
            &root_vnode,
            &root_fs,
            root_inode,
            VFS_VNODE_DIR) != 0)
        return -1;

    if (vfs_mount_init(
            &root_mount,
            &root_fs,
            &root_vnode) != 0)
        return -1;

    return 0;
}

/*
 * Return the mounted root vnode (read only).
 */
vfs_vnode_t *vfs_root(void)
{
    if (!vfs_ready || !root_mount.mounted)
        return NULL;

    return root_mount.root;
}

vfs_filesystem_t *vfs_root_filesystem(void)
{
    if (!vfs_ready || !root_mount.mounted)
        return NULL;

    return root_mount.fs;
}
