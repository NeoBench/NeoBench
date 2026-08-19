/*
 * directory.c
 * NeoBench mkfs.nbfs
 *
 * NBFS v1 directory creation.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/directory.h"

#define NBFS_DIR_TYPE_FILE 1
#define NBFS_DIR_TYPE_DIR  2

/*
 * Create one variable-length NBFS directory entry.
 *
 * On-disk format:
 *
 *   uint64_t inode
 *   uint16_t record_length
 *   uint8_t  name_length
 *   uint8_t  type
 *   char     name[name_length]
 */
static size_t create_entry(
    uint8_t *buffer,
    size_t buffer_size,
    uint64_t inode,
    const char *name,
    uint8_t type)
{
    nbfs_directory_entry_t *entry;
    size_t name_length;
    size_t record_length;

    if (!buffer || !name)
        return 0;

    name_length = strlen(name);

    if (name_length == 0 ||
        name_length > NBFS_MAX_NAME_LENGTH)
        return 0;

    record_length =
        sizeof(nbfs_directory_entry_t) +
        name_length;

    if (record_length > UINT16_MAX)
        return 0;

    if (record_length > buffer_size)
        return 0;

    entry =
        (nbfs_directory_entry_t *)buffer;

    memset(
        entry,
        0,
        record_length);

    entry->inode = inode;

    entry->record_length =
        (uint16_t)record_length;

    entry->name_length =
        (uint8_t)name_length;

    entry->type = type;

    memcpy(
        entry + 1,
        name,
        name_length);

    return record_length;
}


/*
 * Write the root directory to the supplied
 * filesystem data block.
 */
int nbfs_write_root_directory(
    FILE *fp,
    uint64_t block)
{
    uint8_t data[NBFS_DEFAULT_BLOCK_SIZE];

    size_t offset;
    size_t length;

    uint64_t file_offset;

    if (!fp)
        return -1;

    /*
     * Directory blocks must be in the data area.
     */
    if (block < NBFS_DATA_START)
        return -1;

    /*
     * Start with a completely empty directory block.
     */
    memset(
        data,
        0,
        sizeof(data));

    offset = 0;

    /*
     * "." -> root inode.
     */
    length = create_entry(
        data + offset,
        sizeof(data) - offset,
        1,
        ".",
        NBFS_DIR_TYPE_DIR);

    if (length == 0)
        return -1;

    offset += length;

    /*
     * ".." -> root inode because root is its own parent.
     */
    length = create_entry(
        data + offset,
        sizeof(data) - offset,
        1,
        "..",
        NBFS_DIR_TYPE_DIR);

    if (length == 0)
        return -1;

    offset += length;

    /*
     * Convert filesystem block number to byte offset.
     */
    file_offset =
        block *
        (uint64_t)NBFS_DEFAULT_BLOCK_SIZE;

    if (fseek(
            fp,
            (long)file_offset,
            SEEK_SET) != 0)
    {
        return -1;
    }

    /*
     * Write exactly one complete directory block.
     */
    if (fwrite(
            data,
            sizeof(data),
            1,
            fp) != 1)
    {
        return -1;
    }

    fflush(fp);

    return 0;
}
