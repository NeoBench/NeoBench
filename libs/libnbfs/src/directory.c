/*
 * directory.c
 * NeoBench libNBFS
 *
 * NBFS v1 directory operations.
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

static int directory_read_block(
    nbfs_context_t *ctx,
    const nbfs_inode_t *dir,
    uint8_t *buffer)
{
    if (!ctx || !dir || !buffer)
        return -1;

    if (dir->extents[0].block_count == 0)
        return -1;

    return nbfs_read_block(
        ctx,
        dir->extents[0].start_block,
        buffer);
}

int nbfs_lookup(
    nbfs_context_t *ctx,
    uint64_t directory_inode,
    const char *name,
    uint64_t *result_inode)
{
    nbfs_inode_t dir;
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    uint64_t offset = 0;
    size_t name_len;

    if (!ctx || !name || !result_inode)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 || name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (nbfs_read_inode(
            ctx,
            directory_inode,
            &dir) != 0)
        return -1;

    if (dir.mode != 0x4000)
        return -1;

    if (directory_read_block(
            ctx,
            &dir,
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

            if (memcmp(
                    entry_name,
                    name,
                    name_len) == 0)
            {
                *result_inode = entry->inode;
                return 0;
            }
        }

        offset += entry->record_length;
    }

    return -1;
}

/*
 * Rename an entry within a directory.
 *
 * The directory block is rebuilt so the new name may be
 * shorter or longer than the original name.
 *
 * The inode number and entry type are preserved.
 */
int nbfs_rename(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *old_name,
    const char *new_name)
{
    nbfs_inode_t dir;
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    uint8_t rebuilt[NBFS_DEFAULT_BLOCK_SIZE];

    uint64_t offset = 0;
    uint64_t out_offset = 0;

    size_t old_len;
    size_t new_len;

    int found = 0;

    if (!ctx || !old_name || !new_name)
        return -1;

    old_len = strlen(old_name);
    new_len = strlen(new_name);

    if (old_len == 0 || old_len > NBFS_MAX_NAME_LENGTH ||
        new_len == 0 || new_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    /*
     * Never rename "." or "..".
     */
    if (strcmp(old_name, ".") == 0 ||
        strcmp(old_name, "..") == 0 ||
        strcmp(new_name, ".") == 0 ||
        strcmp(new_name, "..") == 0)
        return -1;

    /*
     * Renaming to the same name is a no-op.
     */
    if (strcmp(old_name, new_name) == 0)
        return 0;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &dir) != 0)
        return -1;

    if (dir.mode != 0x4000)
        return -1;

    if (dir.extents[0].block_count == 0)
        return -1;

    /*
     * Destination must not already exist.
     */
    {
        uint64_t existing_inode = 0;

        if (nbfs_lookup(
                ctx,
                parent_inode,
                new_name,
                &existing_inode) == 0)
        {
            return -1;
        }
    }

    if (nbfs_read_block(
            ctx,
            dir.extents[0].start_block,
            block) != 0)
        return -1;

    memset(
        rebuilt,
        0,
        sizeof(rebuilt));

    /*
     * Rebuild the directory block.
     *
     * This also compacts deleted directory holes.
     */
    while (offset + sizeof(nbfs_directory_entry_t) <=
           NBFS_DEFAULT_BLOCK_SIZE)
    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        size_t entry_name_len;
        const char *entry_name;

        uint64_t inode_number;
        uint8_t type;
        size_t output_name_len;

        uint64_t record_len;

        if (entry->record_length == 0)
            break;

        if (entry->record_length <
            sizeof(nbfs_directory_entry_t))
            return -1;

        if (offset + entry->record_length >
            NBFS_DEFAULT_BLOCK_SIZE)
            return -1;

        /*
         * Skip deleted directory holes.
         */
        if (entry->inode == 0)
        {
            offset += entry->record_length;
            continue;
        }

        entry_name_len = entry->name_length;

        if (entry_name_len >
            entry->record_length -
            sizeof(nbfs_directory_entry_t))
            return -1;

        entry_name =
            (const char *)(entry + 1);

        inode_number = entry->inode;
        type = entry->type;

        output_name_len = entry_name_len;

        /*
         * Replace the name for the target entry.
         */
        if (entry_name_len == old_len &&
            memcmp(
                entry_name,
                old_name,
                old_len) == 0)
        {
            output_name_len = new_len;
            found = 1;
        }

        record_len =
            sizeof(nbfs_directory_entry_t) +
            output_name_len;

        /*
         * The entire rebuilt directory must fit
         * inside the existing directory block.
         */
        if (out_offset + record_len >
            NBFS_DEFAULT_BLOCK_SIZE)
            return -1;

        {
            nbfs_directory_entry_t *out =
                (nbfs_directory_entry_t *)
                    (rebuilt + out_offset);

            memset(
                out,
                0,
                (size_t)record_len);

            out->inode = inode_number;
            out->record_length =
                (uint16_t)record_len;
            out->name_length =
                (uint8_t)output_name_len;
            out->type = type;

            if (entry_name_len == old_len &&
                memcmp(
                    entry_name,
                    old_name,
                    old_len) == 0)
            {
                memcpy(
                    out + 1,
                    new_name,
                    new_len);
            }
            else
            {
                memcpy(
                    out + 1,
                    entry_name,
                    entry_name_len);
            }
        }

        out_offset += record_len;
        offset += entry->record_length;
    }

    /*
     * Target was not found.
     *
     * Do not modify the on-disk directory.
     */
    if (!found)
        return -1;

    /*
     * Commit the rebuilt directory block.
     */
    if (nbfs_write_block(
            ctx,
            dir.extents[0].start_block,
            rebuilt) != 0)
        return -1;

    ctx->dirty = true;

    return 0;
}
