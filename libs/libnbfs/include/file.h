#include <stdint.h>
#ifndef NBFS_FILE_H
#define NBFS_FILE_H

#include "libnbfs.h"

int nbfs_create_file(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name);

int nbfs_write_file(
    nbfs_context_t *ctx,
    uint64_t inode,
    const void *buffer,
    uint64_t size);

int nbfs_read_file(
    nbfs_context_t *ctx,
    uint64_t inode,
    uint64_t offset,
    void *buffer,
    uint64_t size);

int nbfs_delete_file(
    nbfs_context_t *ctx,
    uint64_t inode);

#endif
