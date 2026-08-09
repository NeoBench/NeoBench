/*
 * directory.c
 * NeoBench libNBFS
 *
 * NBFS v1 directory operations.
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

    if (name_len == 0 || name_len > 255)
        return -1;

    if (nbfs_read_inode(ctx, directory_inode, &dir) != 0)
        return -1;

    if (directory_read_block(ctx, &dir, block) != 0)
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
            {
                *result_inode = entry->inode;
                return 0;
            }
        }

        offset += entry->record_length;
    }

    return -1;
}
