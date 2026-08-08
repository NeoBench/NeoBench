#include "file.h"

int nbfs_create_file(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name)
{
    (void)ctx;
    (void)parent_inode;
    (void)name;
    return 0;
}

int nbfs_write_file(
    nbfs_context_t *ctx,
    uint64_t inode,
    const void *buffer,
    uint64_t size)
{
    (void)ctx;
    (void)inode;
    (void)buffer;
    (void)size;
    return 0;
}

int nbfs_read_file(
    nbfs_context_t *ctx,
    uint64_t inode,
    void *buffer,
    uint64_t size)
{
    (void)ctx;
    (void)inode;
    (void)buffer;
    (void)size;
    return 0;
}

int nbfs_delete_file(
    nbfs_context_t *ctx,
    uint64_t inode)
{
    (void)ctx;
    (void)inode;
    return 0;
}
