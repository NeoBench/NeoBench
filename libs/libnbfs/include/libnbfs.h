/*
 * libnbfs.h
 * NeoBench Filesystem Library
 *
 * Copyright (c) 2026 NeoBench Project
 * All rights reserved.
 *
 * Public API for the NeoBench NBFS filesystem library.
 */

#ifndef LIBNBFS_H
#define LIBNBFS_H

/*
 * NeoBench Filesystem Library
 * Public API
 */

#include <stdint.h>
#include <stdbool.h>

#include <nbfs/nbfs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Opaque Context
 * -------------------------------------------------------------------------- */

typedef struct nbfs_context nbfs_context_t;

/* --------------------------------------------------------------------------
 * Context Management
 * -------------------------------------------------------------------------- */

nbfs_context_t *nbfs_context_create(void);
void nbfs_context_destroy(nbfs_context_t *ctx);

/* --------------------------------------------------------------------------
 * Image Management
 * -------------------------------------------------------------------------- */

nbfs_context_t *nbfs_create(const char *path);
nbfs_context_t *nbfs_open(const char *path);
void nbfs_close(nbfs_context_t *ctx);

int nbfs_flush(nbfs_context_t *ctx);

/* --------------------------------------------------------------------------
 * Block I/O
 * -------------------------------------------------------------------------- */

int nbfs_read_block(
    nbfs_context_t *ctx,
    uint64_t block,
    void *buffer);

int nbfs_write_block(
    nbfs_context_t *ctx,
    uint64_t block,
    const void *buffer);

/* --------------------------------------------------------------------------
 * Superblock
 * -------------------------------------------------------------------------- */

int nbfs_read_superblock(
    nbfs_context_t *ctx,
    nbfs_superblock_t *superblock);

int nbfs_write_superblock(
    nbfs_context_t *ctx,
    const nbfs_superblock_t *superblock);

int nbfs_verify_superblock(
    const nbfs_superblock_t *superblock);

/* --------------------------------------------------------------------------
 * Bitmap
 * -------------------------------------------------------------------------- */

int nbfs_allocate_block(
    nbfs_context_t *ctx,
    uint64_t *block);

int nbfs_free_block(
    nbfs_context_t *ctx,
    uint64_t block);

/* --------------------------------------------------------------------------
 * Inodes
 * -------------------------------------------------------------------------- */

int nbfs_allocate_inode(
    nbfs_context_t *ctx,
    uint64_t *inode);

int nbfs_free_inode(
    nbfs_context_t *ctx,
    uint64_t inode);

int nbfs_read_inode(
    nbfs_context_t *ctx,
    uint64_t inode,
    nbfs_inode_t *out);

int nbfs_write_inode(
    nbfs_context_t *ctx,
    const nbfs_inode_t *inode);

/* --------------------------------------------------------------------------
 * Directories
 * -------------------------------------------------------------------------- */

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
    void *buffer,
    uint64_t size);

int nbfs_delete_file(
    nbfs_context_t *ctx,
    uint64_t inode);

int nbfs_create_directory(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name);

int nbfs_delete_directory(
    nbfs_context_t *ctx,
    uint64_t inode);

int nbfs_rename(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *old_name,
    const char *new_name);

int nbfs_lookup(
    nbfs_context_t *ctx,
    uint64_t directory_inode,
    const char *name,
    uint64_t *inode);

/* --------------------------------------------------------------------------
 * Journal
 * -------------------------------------------------------------------------- */

int nbfs_journal_begin(nbfs_context_t *ctx);
int nbfs_journal_commit(nbfs_context_t *ctx);
int nbfs_journal_replay(nbfs_context_t *ctx);

/* --------------------------------------------------------------------------
 * CRC32
 * -------------------------------------------------------------------------- */

uint32_t nbfs_crc32(
    const void *buffer,
    uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* LIBNBFS_H */
