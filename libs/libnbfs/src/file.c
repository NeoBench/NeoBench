/*
 * file.c
 * NeoBench libNBFS
 *
 * NBFS v1 file and directory operations.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"
#include <nbfs/nbfs.h>

#define NBFS_DIR_TYPE_FILE 1
#define NBFS_DIR_TYPE_DIR  2

#define NBFS_MODE_DIRECTORY 0x4000
#define NBFS_MODE_FILE      0x8000
#define NBFS_ROOT_INODE     1

/*
 * --------------------------------------------------------------------------
 * Directory helpers
 * --------------------------------------------------------------------------
 */

/*
 * Add an entry to a directory.
 *
 * NBFS v1 directories currently use one data block.
 *
 * Deleted entries are represented by inode == 0 and their original
 * record_length is retained so the space can be reused.
 */
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

    if (inode_number == 0)
        return -1;

    if (type != NBFS_DIR_TYPE_FILE &&
        type != NBFS_DIR_TYPE_DIR)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 ||
        name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (name_len > UINT8_MAX)
        return -1;

    record_len =
        sizeof(nbfs_directory_entry_t) +
        name_len;

    if (record_len > UINT16_MAX)
        return -1;

    if (dir->mode != NBFS_MODE_DIRECTORY)
        return -1;

    if (dir->extents[0].block_count == 0)
        return -1;

    /*
     * NBFS v1 currently supports one directory block.
     */
    if (dir->extents[0].block_count != 1)
        return -1;

    if (nbfs_read_block(
            ctx,
            dir->extents[0].start_block,
            block) != 0)
        return -1;

    /*
     * Walk existing records.
     */
    while (offset + sizeof(nbfs_directory_entry_t) <=
           NBFS_DEFAULT_BLOCK_SIZE)
    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        /*
         * Zero record length marks the unused tail.
         */
        if (entry->record_length == 0)
            break;

        /*
         * Every record must at least contain the header.
         */
        if (entry->record_length <
            sizeof(nbfs_directory_entry_t))
            return -1;

        /*
         * Record must remain inside the block.
         */
        if (offset + entry->record_length >
            NBFS_DEFAULT_BLOCK_SIZE)
            return -1;

        /*
         * Check for duplicate names.
         */
        if (entry->inode != 0)
        {
            size_t available_name;

            available_name =
                entry->record_length -
                sizeof(nbfs_directory_entry_t);

            if (entry->name_length <= available_name &&
                entry->name_length == name_len)
            {
                const char *entry_name =
                    (const char *)(entry + 1);

                if (memcmp(
                        entry_name,
                        name,
                        name_len) == 0)
                {
                    return -1;
                }
            }
        }

        /*
         * Reuse a deleted record if it is large enough.
         */
        if (entry->inode == 0 &&
            entry->record_length >= record_len)
        {
            uint16_t old_length =
                entry->record_length;

            memset(entry, 0, old_length);

            entry->inode = inode_number;
            entry->record_length = old_length;
            entry->name_length = (uint8_t)name_len;
            entry->type = type;

            memcpy(
                entry + 1,
                name,
                name_len);

            if (nbfs_write_block(
                    ctx,
                    dir->extents[0].start_block,
                    block) != 0)
            {
                return -1;
            }

            return 0;
        }

        offset += entry->record_length;
    }

    /*
     * Append at the unused end of the directory.
     */
    if (offset + record_len >
        NBFS_DEFAULT_BLOCK_SIZE)
    {
        return -1;
    }

    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)(block + offset);

        memset(
            entry,
            0,
            record_len);

        entry->inode = inode_number;
        entry->record_length =
            (uint16_t)record_len;
        entry->name_length =
            (uint8_t)name_len;
        entry->type = type;

        memcpy(
            entry + 1,
            name,
            name_len);
    }

    if (nbfs_write_block(
            ctx,
            dir->extents[0].start_block,
            block) != 0)
    {
        return -1;
    }

    return 0;
}


/*
 * Remove a named directory entry from a directory.
 *
 * The record length is retained so the space can be reused.
 */
static int directory_remove_entry(
    nbfs_context_t *ctx,
    nbfs_inode_t *dir,
    const char *name,
    uint64_t *removed_inode)
{
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    uint64_t offset = 0;
    size_t name_len;

    if (!ctx || !dir || !name)
        return -1;

    if (dir->mode != NBFS_MODE_DIRECTORY)
        return -1;

    if (dir->extents[0].block_count == 0)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 ||
        name_len > NBFS_MAX_NAME_LENGTH)
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
            entry->name_length == name_len)
        {
            const char *entry_name =
                (const char *)(entry + 1);

            if (memcmp(
                    entry_name,
                    name,
                    name_len) == 0)
            {
                uint64_t inode_number =
                    entry->inode;

                /*
                 * Do not allow . or .. to be removed.
                 */
                if (name[0] == '.' &&
                    (name[1] == '\0' ||
                     (name[1] == '.' &&
                      name[2] == '\0')))
                {
                    return -1;
                }

                entry->inode = 0;
                entry->name_length = 0;
                entry->type = 0;

                if (nbfs_write_block(
                        ctx,
                        dir->extents[0].start_block,
                        block) != 0)
                {
                    return -1;
                }

                if (removed_inode)
                    *removed_inode = inode_number;

                return 0;
            }
        }

        offset += entry->record_length;
    }

    return -1;
}


/*
 * Determine whether a directory contains any entries other than . and ..
 */
static int directory_is_empty(
    nbfs_context_t *ctx,
    nbfs_inode_t *dir)
{
    uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
    uint64_t offset = 0;

    if (!ctx || !dir)
        return -1;

    if (dir->mode != NBFS_MODE_DIRECTORY)
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

        if (entry->inode != 0)
        {
            const char *name =
                (const char *)(entry + 1);

            /*
             * Ignore . and ..
             */
            if (!(entry->name_length == 1 &&
                  name[0] == '.') &&
                !(entry->name_length == 2 &&
                  name[0] == '.' &&
                  name[1] == '.'))
            {
                return 0;
            }
        }

        offset += entry->record_length;
    }

    return 1;
}


/*
 * Find the parent directory containing a particular inode.
 *
 * This is intentionally a scan for NBFS v1 because the inode does not
 * contain an explicit parent inode number.
 */
static int find_parent_directory(
    nbfs_context_t *ctx,
    uint64_t target_inode,
    uint64_t *parent_inode)
{
    nbfs_superblock_t sb;

    if (!ctx || !parent_inode)
        return -1;

    if (target_inode == NBFS_ROOT_INODE)
        return -1;

    if (nbfs_read_superblock(
            ctx,
            &sb) != 0)
        return -1;

    for (uint64_t candidate = 1;
         candidate <= sb.total_inodes;
         candidate++)
    {
        nbfs_inode_t dir;
        uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
        uint64_t offset = 0;

        if (nbfs_read_inode(
                ctx,
                candidate,
                &dir) != 0)
            continue;

        if (dir.inode_number != candidate)
            continue;

        if (dir.mode != NBFS_MODE_DIRECTORY)
            continue;

        if (dir.extents[0].block_count == 0)
            continue;

        if (nbfs_read_block(
                ctx,
                dir.extents[0].start_block,
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

            if (entry->inode == target_inode)
            {
                const char *name =
                    (const char *)(entry + 1);

                /*
                 * Ignore . because it points to the directory itself.
                 */
                if (!(entry->name_length == 1 &&
                      name[0] == '.'))
                {
                    /*
                     * Ignore .. when looking for the containing
                     * directory.
                     */
                    if (!(entry->name_length == 2 &&
                          name[0] == '.' &&
                          name[1] == '.'))
                    {
                        *parent_inode = candidate;
                        return 0;
                    }
                }
            }

            offset += entry->record_length;
        }
    }

    return -1;
}


/*
 * --------------------------------------------------------------------------
 * File creation
 * --------------------------------------------------------------------------
 */

int nbfs_create_file(
    nbfs_context_t *ctx,
    uint64_t parent_inode,
    const char *name)
{
    nbfs_inode_t parent;
    nbfs_inode_t inode;
    uint64_t new_inode;

    if (!ctx || !name)
        return -1;

    if (strlen(name) == 0 ||
        strlen(name) > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
        return -1;

    if (parent.mode != NBFS_MODE_DIRECTORY)
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
        {
            return -1;
        }
    }

    if (nbfs_allocate_inode(
            ctx,
            &new_inode) != 0)
        return -1;

    memset(
        &inode,
        0,
        sizeof(inode));

    inode.inode_number = new_inode;
    inode.mode = NBFS_MODE_FILE;
    inode.links = 1;
    inode.size = 0;
    inode.created = 0;
    inode.modified = 0;
    inode.accessed = 0;

    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
    {
        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    if (directory_add_entry(
            ctx,
            &parent,
            new_inode,
            name,
            NBFS_DIR_TYPE_FILE) != 0)
    {
        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    ctx->dirty = true;

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Directory creation
 * --------------------------------------------------------------------------
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

    if (name_len == 0 ||
        name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return -1;

    /*
     * Read and validate parent.
     */
    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
    {
        fprintf(stderr, "DEBUG: nbfs_read_inode(%llu) failed\\n",
                (unsigned long long)parent_inode);
        return -1;
    }

    fprintf(stderr,
            "DEBUG: parent inode=%llu mode=%u links=%u size=%llu extent0.start=%llu extent0.count=%u\\n",
            (unsigned long long)parent.inode_number,
            (unsigned)parent.mode,
            (unsigned)parent.links,
            (unsigned long long)parent.size,
            (unsigned long long)parent.extents[0].start_block,
            (unsigned)parent.extents[0].block_count);

    if (parent.inode_number != parent_inode)
    {
        fprintf(stderr, "DEBUG: inode number mismatch\\n");
        return -1;
    }

    if (parent.mode != NBFS_MODE_DIRECTORY)
    {
        fprintf(stderr, "DEBUG: parent is not a directory\\n");
        return -1;
    }

    if (parent.extents[0].block_count == 0)
    {
        fprintf(stderr, "DEBUG: parent has no directory extent\\n");
        return -1;
    }

    /*
     * Check for duplicate name before allocating anything.
     */
    {
        uint64_t existing_inode = 0;

        if (nbfs_lookup(
                ctx,
                parent_inode,
                name,
                &existing_inode) == 0)
        {
            return -1;
        }
    }

    /*
     * Allocate inode first.
     */
    if (nbfs_allocate_inode(
            ctx,
            &new_inode) != 0)
        return -1;

    /*
     * Allocate one directory data block.
     */
    if (nbfs_allocate_block(
            ctx,
            &directory_block) != 0)
    {
        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    /*
     * Build the new directory inode.
     */
    memset(
        &inode,
        0,
        sizeof(inode));

    inode.inode_number = new_inode;
    inode.mode = NBFS_MODE_DIRECTORY;

    /*
     * One link for the directory's own "." entry and one for its
     * parent directory relationship.
     */
    inode.links = 2;

    inode.size = NBFS_DEFAULT_BLOCK_SIZE;

    inode.extents[0].start_block =
        directory_block;

    inode.extents[0].block_count = 1;
    inode.extents[0].flags = 0;

    /*
     * Build the directory block.
     */
    memset(
        block,
        0,
        sizeof(block));

    /*
     * "."
     */
    {
        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)block;

        size_t record_len =
            sizeof(nbfs_directory_entry_t) + 1;

        memset(
            entry,
            0,
            record_len);

        entry->inode = new_inode;
        entry->record_length =
            (uint16_t)record_len;
        entry->name_length = 1;
        entry->type = NBFS_DIR_TYPE_DIR;

        memcpy(
            entry + 1,
            ".",
            1);
    }

    /*
     * ".."
     */
    {
        size_t offset =
            sizeof(nbfs_directory_entry_t) + 1;

        nbfs_directory_entry_t *entry =
            (nbfs_directory_entry_t *)
                (block + offset);

        size_t record_len =
            sizeof(nbfs_directory_entry_t) + 2;

        memset(
            entry,
            0,
            record_len);

        entry->inode = parent_inode;
        entry->record_length =
            (uint16_t)record_len;
        entry->name_length = 2;
        entry->type = NBFS_DIR_TYPE_DIR;

        memcpy(
            entry + 1,
            "..",
            2);
    }

    /*
     * Write directory block before exposing it.
     */
    if (nbfs_write_block(
            ctx,
            directory_block,
            block) != 0)
    {
        nbfs_free_block(
            ctx,
            directory_block);

        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    /*
     * Persist the inode.
     */
    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
    {
        nbfs_free_block(
            ctx,
            directory_block);

        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    /*
     * Add the directory to the parent.
     */
    if (directory_add_entry(
            ctx,
            &parent,
            new_inode,
            name,
            NBFS_DIR_TYPE_DIR) != 0)
    {
        /*
         * The new inode is not reachable from the parent, so it is
         * safe to release its block and inode allocation.
         */
        nbfs_free_block(
            ctx,
            directory_block);

        nbfs_free_inode(
            ctx,
            new_inode);

        return -1;
    }

    /*
     * A child directory increments its parent's link count.
     */
    if (parent.links != UINT16_MAX)
        parent.links++;

    if (nbfs_write_inode(
            ctx,
            &parent) != 0)
    {
        /*
         * At this point the directory entry has been installed.
         * Do not attempt destructive rollback because the parent
         * directory block is already modified.
         */
        return -1;
    }

    ctx->dirty = true;

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Extent management
 * --------------------------------------------------------------------------
 */

/*
 * Free all data blocks referenced by an inode.
 */
static int file_free_extents(
    nbfs_context_t *ctx,
    nbfs_inode_t *inode)
{
    unsigned int i;

    if (!ctx || !inode)
        return -1;

    for (i = 0;
         i < NBFS_EXTENTS_PER_INODE;
         i++)
    {
        uint64_t start;
        uint32_t count;
        uint32_t j;

        start =
            inode->extents[i].start_block;

        count =
            inode->extents[i].block_count;

        if (count == 0)
            continue;

        for (j = 0;
             j < count;
             j++)
        {
            if (nbfs_free_block(
                    ctx,
                    start + j) != 0)
            {
                return -1;
            }
        }

        inode->extents[i].start_block = 0;
        inode->extents[i].block_count = 0;
        inode->extents[i].flags = 0;
    }

    return 0;
}


/*
 * Allocate one block per extent.
 *
 * NBFS v1 has 12 extent slots, so the current maximum file size is
 * 12 * NBFS_DEFAULT_BLOCK_SIZE.
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
        {
            return -1;
        }

        inode->extents[extent].start_block =
            block;

        inode->extents[extent].block_count = 1;
        inode->extents[extent].flags = 0;

        allocated++;
        extent++;
    }

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * File write
 * --------------------------------------------------------------------------
 */

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

    if (inode.inode_number != inode_number)
        return -1;

    if (inode.mode != NBFS_MODE_FILE)
        return -1;

    if (size == 0)
        blocks_needed = 0;
    else
        blocks_needed =
            (size +
             NBFS_DEFAULT_BLOCK_SIZE - 1) /
            NBFS_DEFAULT_BLOCK_SIZE;

    if (blocks_needed > NBFS_EXTENTS_PER_INODE)
        return -1;

    /*
     * Free the previous file contents.
     */
    if (file_free_extents(
            ctx,
            &inode) != 0)
        return -1;

    memset(
        inode.extents,
        0,
        sizeof(inode.extents));

    /*
     * Allocate the new contents.
     */
    if (blocks_needed != 0)
    {
        if (file_allocate_extents(
                ctx,
                &inode,
                blocks_needed) != 0)
        {
            /*
             * Best-effort cleanup of anything allocated so far.
             */
            (void)file_free_extents(
                ctx,
                &inode);

            memset(
                inode.extents,
                0,
                sizeof(inode.extents));

            return -1;
        }
    }

    remaining = size;

    for (extent = 0;
         extent < NBFS_EXTENTS_PER_INODE &&
         remaining != 0;
         extent++)
    {
        uint8_t block_buffer[
            NBFS_DEFAULT_BLOCK_SIZE];

        uint64_t to_copy;

        if (inode.extents[extent].block_count == 0)
            return -1;

        if (inode.extents[extent].block_count != 1)
            return -1;

        to_copy = remaining;

        if (to_copy >
            NBFS_DEFAULT_BLOCK_SIZE)
        {
            to_copy =
                NBFS_DEFAULT_BLOCK_SIZE;
        }

        memset(
            block_buffer,
            0,
            sizeof(block_buffer));

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


/*
 * --------------------------------------------------------------------------
 * File read
 * --------------------------------------------------------------------------
 */

int nbfs_read_file(
    nbfs_context_t *ctx,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    uint64_t size)
{
    nbfs_inode_t inode;
    uint64_t remaining;
    uint64_t copied = 0;
    uint64_t file_position;
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

    if (inode.inode_number != inode_number)
        return -1;

    if (inode.mode != NBFS_MODE_FILE)
        return -1;

    /*
     * The requested offset must be inside the file.
     */
    if (offset > inode.size)
        return -1;

    /*
     * A zero-length read at EOF is valid.
     */
    if (size == 0)
        return 0;

    /*
     * Do not permit reads beyond EOF.
     */
    if (size > inode.size - offset)
        return -1;

    remaining = size;
    file_position = offset;

    /*
     * NBFS v1 currently stores one filesystem block per extent.
     */
    for (extent = 0;
         extent < NBFS_EXTENTS_PER_INODE &&
         remaining != 0;
         extent++)
    {
        uint8_t block_buffer[
            NBFS_DEFAULT_BLOCK_SIZE];

        uint64_t extent_start;
        uint64_t block_offset;
        uint64_t to_copy;

        if (inode.extents[extent].block_count == 0)
            break;

        if (inode.extents[extent].block_count != 1)
            return -1;

        extent_start =
            (uint64_t)extent *
            NBFS_DEFAULT_BLOCK_SIZE;

        /*
         * The requested file position is beyond this extent.
         */
        if (file_position >=
            extent_start + NBFS_DEFAULT_BLOCK_SIZE)
        {
            continue;
        }

        /*
         * Calculate the byte offset within this block.
         */
        if (file_position > extent_start)
            block_offset =
                file_position - extent_start;
        else
            block_offset = 0;

        if (nbfs_read_block(
                ctx,
                inode.extents[extent].start_block,
                block_buffer) != 0)
        {
            return -1;
        }

        to_copy =
            NBFS_DEFAULT_BLOCK_SIZE -
            block_offset;

        if (to_copy > remaining)
            to_copy = remaining;

        memcpy(
            (uint8_t *)buffer + copied,
            block_buffer + block_offset,
            (size_t)to_copy);

        copied += to_copy;
        remaining -= to_copy;
        file_position += to_copy;
    }

    return remaining == 0 ? 0 : -1;
}

/*
 * --------------------------------------------------------------------------
 * File deletion
 * --------------------------------------------------------------------------
 */

int nbfs_delete_file(
    nbfs_context_t *ctx,
    uint64_t inode_number)
{
    nbfs_inode_t inode;
    nbfs_inode_t parent;
    uint64_t parent_inode;

    if (!ctx)
        return -1;

    if (inode_number < 2)
        return -1;

    /*
     * Read and validate target inode.
     */
    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
        return -1;

    if (inode.inode_number != inode_number)
        return -1;

    if (inode.mode != NBFS_MODE_FILE)
        return -1;

    /*
     * Find the directory containing the file.
     */
    if (find_parent_directory(
            ctx,
            inode_number,
            &parent_inode) != 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
        return -1;

    if (directory_remove_entry(
            ctx,
            &parent,
            NULL,
            NULL) != 0)
    {
        /*
         * This branch is never used because removal by inode is
         * performed below. Keep the actual operation explicit.
         */
    }

    /*
     * Locate and remove the target entry by inode.
     */
    {
        uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
        uint64_t offset = 0;
        int found = 0;

        if (nbfs_read_block(
                ctx,
                parent.extents[0].start_block,
                block) != 0)
            return -1;

        while (offset + sizeof(nbfs_directory_entry_t) <=
               NBFS_DEFAULT_BLOCK_SIZE)
        {
            nbfs_directory_entry_t *entry =
                (nbfs_directory_entry_t *)
                    (block + offset);

            if (entry->record_length == 0)
                break;

            if (entry->record_length <
                sizeof(nbfs_directory_entry_t))
                return -1;

            if (offset + entry->record_length >
                NBFS_DEFAULT_BLOCK_SIZE)
                return -1;

            if (entry->inode == inode_number)
            {
                entry->inode = 0;
                entry->name_length = 0;
                entry->type = 0;

                if (nbfs_write_block(
                        ctx,
                        parent.extents[0].start_block,
                        block) != 0)
                {
                    return -1;
                }

                found = 1;
                break;
            }

            offset += entry->record_length;
        }

        if (!found)
            return -1;
    }

    /*
     * Free file data blocks.
     */
    if (file_free_extents(
            ctx,
            &inode) != 0)
        return -1;

    memset(
        inode.extents,
        0,
        sizeof(inode.extents));

    inode.size = 0;
    inode.links = 0;

    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
        return -1;

    /*
     * Release inode bitmap allocation.
     */
    if (nbfs_free_inode(
            ctx,
            inode_number) != 0)
        return -1;

    ctx->dirty = true;

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Directory deletion
 * --------------------------------------------------------------------------
 */

int nbfs_delete_directory(
    nbfs_context_t *ctx,
    uint64_t inode_number)
{
    nbfs_inode_t inode;
    nbfs_inode_t parent;
    uint64_t parent_inode;

    if (!ctx)
        return -1;

    /* Root directory cannot be deleted. */
    if (inode_number <= NBFS_ROOT_INODE)
        return -1;

    /* Read and validate target inode. */
    if (nbfs_read_inode(
            ctx,
            inode_number,
            &inode) != 0)
        return -1;

    if (inode.inode_number != inode_number)
        return -1;

    if (inode.mode != NBFS_MODE_DIRECTORY)
        return -1;

    /*
     * Directory must contain only . and ..
     */
    if (directory_is_empty(ctx, &inode) != 1)
        return -1;

    /* Find the parent directory. */
    if (find_parent_directory(
            ctx,
            inode_number,
            &parent_inode) != 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            parent_inode,
            &parent) != 0)
        return -1;

    if (parent.mode != NBFS_MODE_DIRECTORY)
        return -1;

    /*
     * Remove the child entry from the parent directory.
     *
     * NBFS directory entries are variable length, so walk
     * using record_length rather than assuming fixed entries.
     */
    {
        uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];
        uint64_t offset = 0;
        int found = 0;

        if (parent.extents[0].block_count == 0)
            return -1;

        if (nbfs_read_block(
                ctx,
                parent.extents[0].start_block,
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

            if (entry->inode == inode_number)
            {
                entry->inode = 0;
                entry->name_length = 0;
                entry->type = 0;

                if (nbfs_write_block(
                        ctx,
                        parent.extents[0].start_block,
                        block) != 0)
                    return -1;

                found = 1;
                break;
            }

            offset += entry->record_length;
        }

        if (!found)
            return -1;
    }

    /* Release all blocks owned by the directory. */
    if (file_free_extents(ctx, &inode) != 0)
        return -1;

    memset(
        inode.extents,
        0,
        sizeof(inode.extents));

    inode.size = 0;
    inode.links = 0;

    /* Persist the now-free inode contents before releasing it. */
    if (nbfs_write_inode(
            ctx,
            &inode) != 0)
        return -1;

    /* Release the inode bitmap allocation. */
    if (nbfs_free_inode(
            ctx,
            inode_number) != 0)
        return -1;

    /* The parent loses one child-directory link. */
    if (parent.links > 0)
        parent.links--;

    if (nbfs_write_inode(
            ctx,
            &parent) != 0)
        return -1;

    ctx->dirty = true;

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Rename
 * --------------------------------------------------------------------------
 */

