#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "nbfs_vfs.h"

int vfs_nbfs_bind(
    vfs_filesystem_t *fs,
    nbfs_context_t *ctx)
{
    if (!fs || !ctx)
        return -1;

    fs->private_data = ctx;
    fs->lookup = vfs_nbfs_lookup;
    fs->get_size = vfs_nbfs_get_size;
    fs->read_file = vfs_nbfs_read;

    return 0;
}

int vfs_nbfs_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
)
{
    nbfs_context_t *ctx;
    nbfs_inode_t inode;

    if (!fs || !fs->private_data ||
        !name || !result_inode || !result_mode)
        return -1;

    ctx = (nbfs_context_t *)fs->private_data;

    if (nbfs_lookup(
            ctx,
            parent_inode,
            name,
            result_inode) != 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            *result_inode,
            &inode) != 0)
        return -1;

    *result_mode = inode.mode;

    return 0;
}

int vfs_nbfs_get_size(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t *size)
{
    nbfs_context_t *ctx;
    nbfs_inode_t inode;

    if (!fs || !fs->private_data || !size)
        return -1;

    ctx = (nbfs_context_t *)fs->private_data;

    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
        return -1;

    *size = inode.size;

    return 0;
}

ssize_t vfs_nbfs_read(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    nbfs_context_t *ctx;

    if (!fs || !fs->private_data || !buffer)
        return -1;

    ctx = (nbfs_context_t *)fs->private_data;

    if (nbfs_read_file(
            ctx,
            inode_number,
            offset,
            buffer,
            (uint64_t)size) != 0)
        return -1;

    return (ssize_t)size;
}
