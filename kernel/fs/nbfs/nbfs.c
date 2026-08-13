#include <stdint.h>

#include "nbfs.h"

static int nbfs_ready;
static nbfs_mount_t nbfs_mount;

int nbfs_kernel_init(void)
{
    nbfs_ready = 1;

    nbfs_mount.device = 0;
    nbfs_mount.block_size = 0;
    nbfs_mount.block_count = 0;
    nbfs_mount.root_inode = 0;
    nbfs_mount.mounted = 0;

    return 0;
}

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device
)
{
    if (!fs || !device || !nbfs_ready)
        return -1;

    nbfs_mount.device = device;

    /*
     * Superblock parsing will be connected to the NBFS
     * on-disk format in the next stage.
     */
    nbfs_mount.block_size = fs->block_size;
    nbfs_mount.block_count = 0;
    nbfs_mount.root_inode = 1;
    nbfs_mount.mounted = 1;

    fs->private_data = &nbfs_mount;
    fs->lookup = nbfs_kernel_lookup;

    return 0;
}

int nbfs_kernel_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    unsigned long *result_type
)
{
    if (!fs || !name || !result_inode || !result_type)
        return -1;

    if (!nbfs_mount.mounted)
        return -1;

    (void)parent_inode;

    *result_inode = 0;
    *result_type = 0;

    /*
     * Directory lookup will be implemented after the
     * on-disk superblock/inode layer is connected.
     */
    return -1;
}

void nbfs_kernel_shutdown(void)
{
    nbfs_mount.device = 0;
    nbfs_mount.block_size = 0;
    nbfs_mount.block_count = 0;
    nbfs_mount.root_inode = 0;
    nbfs_mount.mounted = 0;

    nbfs_ready = 0;
}
