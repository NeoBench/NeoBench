/*
 * file.c
 * NeoBench libNBFS
 *
 * NBFS v1 file operations.
 *
 * Copyright (c) 2026 NeoBench Project
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"
#include <nbfs/nbfs.h>

#define NBFS_DIR_TYPE_FILE 1
#define NBFS_DIR_TYPE_DIR  2

static int directory_add_entry(
    nbfs_context_t *ctx,
    nbfs_inode_t *dir,
    uint64_t inode_number,
    const char *name,
    uint8_t type)
{
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    uint64_t offset = 0;
    size_t name_len;
    size_t record_len;

    if (!ctx || !dir || !name)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 || name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    record_len =
        sizeof(nbfs_directory_entry_t) + name_len;

    if (record_len > UINT16_MAX)
        return -1;

    if (dir->extents[0].block_count == 0)
        return -1;

    if (nbfs_read_block(
            ctx,
            dir->extents[0].start_block,
            block) != 0)
        return -1;

    while (offset + sizeof(nbfs_directory_entry_t) <=
           NBFS_DEFAULT_BLOCK_SIZE)
    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        if (entry->record_length == 0)
            break;

        if (entry->record_length <
            sizeof(nbfs_directory_entry_t))
            return -1;

        if (offset + entry->record_length >
            NBFS_DEFAULT_BLOCK_SIZE)
            return -1;

        if (entry->inode != 0 &&
            entry->name_length == name_len &&
            entry->name_length <=
                entry->record_length -
                sizeof(nbfs_directory_entry_t))
        {
            const char *entry_name =
                (const char *)(entry + 1);

            if (memcmp(entry_name, name, name_len) == 0)
                return -1;
        }

        if (entry->inode == 0 &&
            entry->record_length >= record_len)
        {
            uint16_t old_length = entry->record_length;

            memset(entry, 0, old_length);

            entry->inode = inode_number;
            entry->record_length = old_length;
            entry->name_length = (uint8_t)name_len;
            entry->type = type;

            memcpy(entry + 1, name, name_len);

            if (nbfs_write_block(
                    ctx,
                    dir->extents[0].start_block,
                    block) != 0)
                return -1;

            return 0;
        }

        offset += entry->record_length;
    }

    if (offset + record_len > NBFS_DEFAULT_BLOCK_SIZE)
        return -1;

    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        memset(entry, 0, record_len);

        entry->inode = inode_number;
        entry->record_length = (uint16_t)record_len;
        entry->name_length = (uint8_t)name_len;
        entry->type = NBFS_DIR_TYPE_FILE;

        memcpy(entry + 1, name, name_len);
    }

    if (nbfs_write_block(
            ctx,
            dir->extents[0].start_block,
            block) != 0)
        return -1;

    return 0;
}

int nbfs_create_file(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name)
{
    nbfs_inode_t parent;
    nbfs_inode_t inode;
    uint64_t new_inode;
    int result;

    if (!ctx || !name)
        return -1;

    if (strlen(name) == 0 || strlen(name) > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
        return -1;

    if (parent.mode != 0x4000)
        return -1;

    if (parent.extents[0].block_count == 0)
        return -1;

    {
        uint64_t existing_inode = 0;

        if (nbfs_lookup(
                ctx,
                parent_inode,
                name,
                &existing_inode) == 0)
            return -1;
    }

    if (nbfs_allocate_inode(
            ctx,
            &new_inode) != 0)
        return -1;

    memset(&inode, 0, sizeof(inode));

    inode.inode_number = new_inode;
    inode.mode = 0x8000;
    inode.links = 1;
    inode.size = 0;

    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
    {
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    result = directory_add_entry(
        ctx,
        &parent,
        new_inode,
        name,
        NBFS_DIR_TYPE_FILE);

    if (result != 0)
    {
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    ctx->dirty = true;

    return 0;
}

/*
 * Free all data blocks currently referenced by an inode.
 */

int nbfs_create_directory(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name)
{
    nbfs_inode_t parent;
    nbfs_inode_t inode;
    uint64_t new_inode;
    uint64_t directory_block;
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    size_t name_len;

    if (!ctx || !name)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 || name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
        return -1;

    if (parent.mode != 0x4000)
        return -1;

    if (parent.extents[0].block_count == 0)
        return -1;

    {
        uint64_t existing_inode = 0;

        if (nbfs_lookup(
                ctx,
                parent_inode,
                name,
                &existing_inode) == 0)
            return -1;
    }

    if (nbfs_allocate_inode(
            ctx,
            &new_inode) != 0)
        return -1;

    if (nbfs_allocate_block(
            ctx,
            &directory_block) != 0)
    {
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    memset(&inode, 0, sizeof(inode));

    inode.inode_number = new_inode;
    inode.mode = 0x4000;
    inode.links = 2;
    inode.size = 0;

    inode.extents[0].start_block = directory_block;
    inode.extents[0].block_count = 1;
    inode.extents[0].flags = 0;

    memset(block, 0, sizeof(block));

    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)block;

        entry->inode = new_inode;
        entry->record_length =
            (uint16_t)(sizeof(nbfs_directory_entry_t) + 1);
        entry->name_length = 1;
        entry->type = NBFS_DIR_TYPE_DIR;

        memcpy(entry + 1, ".", 1);
    }

    {
        size_t offset =
            sizeof(nbfs_directory_entry_t) + 1;

        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        entry->inode = parent_inode;
        entry->record_length =
            (uint16_t)(sizeof(nbfs_directory_entry_t) + 2);
        entry->name_length = 2;
        entry->type = NBFS_DIR_TYPE_DIR;

        memcpy(entry + 1, "..", 2);
    }

    if (nbfs_write_block(
            ctx,
            directory_block,
            block) != 0)
    {
        nbfs_free_block(ctx, directory_block);
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
    {
        nbfs_free_block(ctx, directory_block);
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    if (directory_add_entry(
            ctx,
            &parent,
            new_inode,
            name,
            NBFS_DIR_TYPE_DIR) != 0)
    {
        nbfs_free_block(ctx, directory_block);
        nbfs_free_inode(ctx, new_inode);
        return -1;
    }

    parent.links++;

    if (nbfs_write_inode(
            ctx,
            &parent) != 0)
        return -1;

    ctx->dirty = true;

    return 0;
}

static int file_free_extents(
    nbfs_context_t *ctx,
    nbfs_inode_t *inode)
{
    unsigned int i;

    if (!ctx || !inode)
        return -1;

    for (i = 0; i < NBFS_EXTENTS_PER_INODE; i++)
    {
        uint64_t start;
        uint32_t count;
        uint32_t j;

        start = inode->extents[i].start_block;
        count = inode->extents[i].block_count;

        if (count == 0)
            continue;

        for (j = 0; j < count; j++)
        {
            if (nbfs_free_block(
                    ctx,
                    start + j) != 0)
                return -1;
        }

        inode->extents[i].start_block = 0;
        inode->extents[i].block_count = 0;
        inode->extents[i].flags = 0;
    }

    return 0;
}

/*
 * Allocate blocks for a file.
 *
 * NBFS v1 uses up to 12 extents. Each allocated block is initially
 * represented as its own extent. This is simple and preserves the
 * existing on-disk format.
 */
static int file_allocate_extents(
    nbfs_context_t *ctx,
    nbfs_inode_t *inode,
    uint64_t blocks_needed)
{
    uint64_t allocated = 0;
    unsigned int extent = 0;

    if (!ctx || !inode)
        return -1;

    while (allocated < blocks_needed)
    {
        uint64_t block;

        if (extent >= NBFS_EXTENTS_PER_INODE)
            return -1;

        if (nbfs_allocate_block(
                ctx,
                &block) != 0)
            return -1;

        inode->extents[extent].start_block = block;
        inode->extents[extent].block_count = 1;
        inode->extents[extent].flags = 0;

        allocated++;
        extent++;
    }

    return 0;
}

int nbfs_write_file(
    nbfs_context_t *ctx,
    uint64_t inode_number,
    const void *buffer,
    uint64_t size)
{
    nbfs_inode_t inode;
    uint64_t blocks_needed;
    uint64_t remaining;
    uint64_t copied = 0;
    unsigned int extent;

    if (!ctx)
        return -1;

    if (size > 0 && !buffer)
        return -1;

    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
        return -1;

    if (inode.mode != 0x8000)
        return -1;

    /*
     * Empty file: release existing data and leave zero extents.
     */
    if (size == 0)
    {
        if (file_free_extents(ctx, &inode) != 0)
            return -1;

        inode.size = 0;

        if (nbfs_write_inode(ctx, &inode) != 0)
            return -1;

        ctx->dirty = true;
        return 0;
    }

    blocks_needed =
        (size + NBFS_DEFAULT_BLOCK_SIZE - 1) /
        NBFS_DEFAULT_BLOCK_SIZE;

    /*
     * This v1 implementation has twelve extent slots.
     */
    if (blocks_needed > NBFS_EXTENTS_PER_INODE)
        return -1;

    /*
     * Replace the old file allocation.
     */
    if (file_free_extents(ctx, &inode) != 0)
        return -1;

    memset(
        inode.extents,
        0,
        sizeof(inode.extents));

    if (file_allocate_extents(
            ctx,
            &inode,
            blocks_needed) != 0)
    {
        /*
         * Best-effort cleanup of partially allocated blocks.
         */
        (void)file_free_extents(ctx, &inode);
        return -1;
    }

    remaining = size;

    for (extent = 0;
         extent < NBFS_EXTENTS_PER_INODE &&
         remaining != 0;
         extent++)
    {
        uint8_t block_buffer[NBFS_DEFAULT_BLOCK_SIZE];
        uint64_t to_copy;

        if (inode.extents[extent].block_count == 0)
            break;

        to_copy = remaining;

        if (to_copy > NBFS_DEFAULT_BLOCK_SIZE)
            to_copy = NBFS_DEFAULT_BLOCK_SIZE;

        memset(block_buffer, 0, sizeof(block_buffer));

        memcpy(
            block_buffer,
            (const uint8_t *)buffer + copied,
            (size_t)to_copy);

        if (nbfs_write_block(
                ctx,
                inode.extents[extent].start_block,
                block_buffer) != 0)
        {
            return -1;
        }

        copied += to_copy;
        remaining -= to_copy;
    }

    inode.size = size;

    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
        return -1;

    ctx->dirty = true;

    return 0;
}

int nbfs_read_file(
    nbfs_context_t *ctx,
    uint64_t inode_number,
    void *buffer,
    uint64_t size)
{
    nbfs_inode_t inode;
    uint64_t remaining;
    uint64_t copied = 0;
    unsigned int extent;

    if (!ctx)
        return -1;

    if (size > 0 && !buffer)
        return -1;

    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
        return -1;

    if (inode.mode != 0x8000)
        return -1;

    if (size > inode.size)
        return -1;

    if (size == 0)
        return 0;

    remaining = size;

    for (extent = 0;
         extent < NBFS_EXTENTS_PER_INODE &&
         remaining != 0;
         extent++)
    {
        uint8_t block_buffer[NBFS_DEFAULT_BLOCK_SIZE];
        uint64_t to_copy;

        if (inode.extents[extent].block_count == 0)
            return -1;

        if (inode.extents[extent].block_count != 1)
            return -1;

        if (nbfs_read_block(
                ctx,
                inode.extents[extent].start_block,
                block_buffer) != 0)
            return -1;

        to_copy = remaining;

        if (to_copy > NBFS_DEFAULT_BLOCK_SIZE)
            to_copy = NBFS_DEFAULT_BLOCK_SIZE;

        memcpy(
            (uint8_t *)buffer + copied,
            block_buffer,
            (size_t)to_copy);

        copied += to_copy;
        remaining -= to_copy;
    }

    return remaining == 0 ? 0 : -1;
}

int nbfs_delete_file(
    nbfs_context_t *ctx,
    uint64_t inode_number)
{
    nbfs_superblock_t sb;
    nbfs_inode_t inode;
    nbfs_inode_t dir;
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];

    if (!ctx || inode_number < 2)
    {
        fprintf(stderr, "DELETE: invalid arguments\n");
        return -1;
    }

    fprintf(stderr,
            "DELETE: target inode=%llu\n",
            (unsigned long long)inode_number);

    if (nbfs_read_inode(ctx, inode_number, &inode) != 0)
    {
        fprintf(stderr, "DELETE: read target inode failed\n");
        return -1;
    }

    fprintf(stderr,
            "DELETE: inode=%llu mode=0x%04x size=%llu\n",
            (unsigned long long)inode.inode_number,
            inode.mode,
            (unsigned long long)inode.size);

    if (inode.inode_number != inode_number)
    {
        fprintf(stderr, "DELETE: inode number mismatch\n");
        return -1;
    }

    if (inode.mode != 0x8000)
    {
        fprintf(stderr, "DELETE: not a regular file\n");
        return -1;
    }

    if (nbfs_read_superblock(ctx, &sb) != 0)
    {
        fprintf(stderr, "DELETE: read superblock failed\n");
        return -1;
    }

    fprintf(stderr,
            "DELETE: total_inodes=%llu\n",
            (unsigned long long)sb.total_inodes);

    /*
     * Find the directory containing this inode.
     */
    for (uint64_t candidate = 1;
         candidate <= sb.total_inodes;
         candidate++)
    {
        uint64_t offset = 0;

        if (nbfs_read_inode(ctx, candidate, &dir) != 0)
            continue;

        if (dir.inode_number != candidate)
            continue;

        if (dir.mode != 0x4000)
            continue;

        if (dir.extents[0].block_count == 0)
            continue;

        fprintf(stderr,
                "DELETE: scanning directory inode=%llu block=%llu\n",
                (unsigned long long)candidate,
                (unsigned long long)dir.extents[0].start_block);

        if (nbfs_read_block(
                ctx,
                dir.extents[0].start_block,
                block) != 0)
        {
            fprintf(stderr, "DELETE: read directory block failed\n");
            return -1;
        }

        while (offset + sizeof(nbfs_directory_entry_t) <=
               NBFS_DEFAULT_BLOCK_SIZE)
        {
            nbfs_directory_entry_t *entry =
                (nbfs_directory_entry_t *)(block + offset);

            if (entry->record_length == 0)
                break;

            if (entry->record_length <
                sizeof(nbfs_directory_entry_t))
            {
                fprintf(stderr,
                        "DELETE: invalid directory record length\n");
                return -1;
            }

            if (offset + entry->record_length >
                NBFS_DEFAULT_BLOCK_SIZE)
            {
                fprintf(stderr,
                        "DELETE: directory record exceeds block\n");
                return -1;
            }

            fprintf(stderr,
                    "DELETE: entry inode=%llu name_len=%u type=%u\n",
                    (unsigned long long)entry->inode,
                    entry->name_length,
                    entry->type);

            if (entry->inode == inode_number)
            {
                fprintf(stderr,
                        "DELETE: found target in directory %llu\n",
                        (unsigned long long)candidate);

                entry->inode = 0;
                entry->name_length = 0;
                entry->type = 0;

                if (nbfs_write_block(
                        ctx,
                        dir.extents[0].start_block,
                        block) != 0)
                {
                    fprintf(stderr,
                            "DELETE: write directory block failed\n");
                    return -1;
                }

                fprintf(stderr,
                        "DELETE: directory entry removed\n");

                /*
                 * Now release the file data blocks.
                 */
                if (file_free_extents(ctx, &inode) != 0)
                {
                    fprintf(stderr,
                            "DELETE: file_free_extents failed\n");
                    return -1;
                }

                fprintf(stderr,
                        "DELETE: file extents freed\n");

                memset(
                    inode.extents,
                    0,
                    sizeof(inode.extents));

                inode.size = 0;
                inode.links = 0;

                if (nbfs_write_inode(ctx, &inode) != 0)
                {
                    fprintf(stderr,
                            "DELETE: write cleared inode failed\n");
                    return -1;
                }

                fprintf(stderr,
                        "DELETE: inode cleared\n");

                if (nbfs_free_inode(
                        ctx,
                        inode_number) != 0)
                {
                    fprintf(stderr,
                            "DELETE: nbfs_free_inode failed\n");
                    return -1;
                }

                fprintf(stderr,
                        "DELETE: inode bitmap released\n");

                ctx->dirty = true;

                fprintf(stderr,
                        "DELETE: SUCCESS\n");

                return 0;
            }

            offset += entry->record_length;
        }
    }

    fprintf(stderr,
            "DELETE: target inode not found in any directory\n");

    return -1;
}

int nbfs_delete_directory(
    nbfs_context_t *ctx,
    uint64_t inode_number)
{
    nbfs_superblock_t sb;
    nbfs_inode_t inode;
    nbfs_inode_t dir;
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];

    if (!ctx || inode_number < 2)
        return -1;

    if (nbfs_read_inode(ctx, inode_number, &inode) != 0)
        return -1;

    if (inode.inode_number != inode_number)
        return -1;

    /* NBFS directory mode. */
    if (inode.mode != 0x4000)
        return -1;

    if (inode.extents[0].block_count == 0)
        return -1;

    if (nbfs_read_superblock(ctx, &sb) != 0)
        return -1;

    /*
     * A directory can only be removed when it contains
     * "." and "..". Any other live entry makes it non-empty.
     */
    if (nbfs_read_block(
            ctx,
            inode.extents[0].start_block,
            block) != 0)
    {
        return -1;
    }

    uint64_t offset = 0;

    while (offset + sizeof(nbfs_directory_entry_t) <=
           NBFS_DEFAULT_BLOCK_SIZE)
    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        if (entry->record_length == 0)
            break;

        if (entry->record_length <
            sizeof(nbfs_directory_entry_t))
        {
            return -1;
        }

        if (offset + entry->record_length >
            NBFS_DEFAULT_BLOCK_SIZE)
        {
            return -1;
        }

        if (entry->inode != 0)
        {
            const char *name =
                (const char *)(entry + 1);

            /*
             * "." and ".." are the only permitted live
             * entries in an empty directory.
             */
            int is_dot =
                entry->name_length == 1 &&
                name[0] == '.';

            int is_dotdot =
                entry->name_length == 2 &&
                name[0] == '.' &&
                name[1] == '.';

            if (!is_dot && !is_dotdot)
                return -1;
        }

        offset += entry->record_length;
    }

    /*
     * Find the parent directory and remove this
     * directory's entry from it.
     */
    for (uint64_t candidate = 1;
         candidate <= sb.total_inodes;
         candidate++)
    {
        uint64_t dir_offset = 0;

        if (nbfs_read_inode(ctx, candidate, &dir) != 0)
            continue;

        if (dir.inode_number != candidate)
            continue;

        if (dir.mode != 0x4000)
            continue;

        if (dir.extents[0].block_count == 0)
            continue;

        if (nbfs_read_block(
                ctx,
                dir.extents[0].start_block,
                block) != 0)
        {
            return -1;
        }

        while (dir_offset + sizeof(nbfs_directory_entry_t) <=
               NBFS_DEFAULT_BLOCK_SIZE)
        {
            nbfs_directory_entry_t *entry =
                (nbfs_directory_entry_t *)(block + dir_offset);

            if (entry->record_length == 0)
                break;

            if (entry->record_length <
                sizeof(nbfs_directory_entry_t))
            {
                return -1;
            }

            if (dir_offset + entry->record_length >
                NBFS_DEFAULT_BLOCK_SIZE)
            {
                return -1;
            }

            if (entry->inode == inode_number)
            {
                entry->inode = 0;
                entry->name_length = 0;
                entry->type = 0;

                if (nbfs_write_block(
                        ctx,
                        dir.extents[0].start_block,
                        block) != 0)
                {
                    return -1;
                }

                /*
                 * The directory owns its directory data block.
                 */
                if (nbfs_free_block(
                        ctx,
                        inode.extents[0].start_block) != 0)
                {
                    return -1;
                }

                memset(
                    inode.extents,
                    0,
                    sizeof(inode.extents));

                inode.size = 0;
                inode.links = 0;

                if (nbfs_write_inode(ctx, &inode) != 0)
                    return -1;

                if (nbfs_free_inode(
                        ctx,
                        inode_number) != 0)
                {
                    return -1;
                }

                ctx->dirty = true;

                return 0;
            }

            dir_offset += entry->record_length;
        }
    }

    return -1;
}

