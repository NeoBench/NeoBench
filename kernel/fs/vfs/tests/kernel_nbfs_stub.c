/*
 * kernel_nbfs_stub.c
 *
 * Stubs for the kernel-only NBFS root mount functions so the
 * userspace VFS tests keep linking.  These tests exercise the
 * userspace nbfs_vfs adapter (libnbfs), not the kernel NBFS
 * driver; the real kernel path is covered by
 * kernel/fs/nbfs/tests/test_kernel_root.
 */

#include <stdint.h>
#include <sys/types.h>

#include "vfs/vfs.h"
#include "block/device.h"
#include "nbfs.h"

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device)
{
    (void)fs;
    (void)device;
    return 0;
}

int nbfs_kernel_root_inode(
    const vfs_filesystem_t *fs,
    uint64_t *inode)
{
    (void)fs;
    *inode = 1;
    return 0;
}