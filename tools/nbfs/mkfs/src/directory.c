/*
 * directory.c
 * NeoBench mkfs.nbfs
 *
 * NBFS root directory implementation.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/directory.h"

/*
 * Fixed-size directory entry.
 *
 * The first 12 bytes match the NBFS on-disk directory-entry
 * header:
 *
 *   uint64_t inode
 *   uint16_t record_length
 *   uint8_t  name_length
 *   uint8_t  type
 *
 * The remaining bytes contain the filename.
 */
typedef struct
{
    uint64_t inode;
    uint16_t record_length;
    uint8_t  name_length;
    uint8_t  type;
    char     name[252];

} nbfs_dirent_t;


/*
 * Create one directory entry.
 */
static void create_entry(
    nbfs_dirent_t *entry,
    uint64_t inode,
    const char *name
)
{
    size_t length;

    memset(entry, 0, sizeof(*entry));

    entry->inode = inode;

    length = strlen(name);

    if (length > sizeof(entry->name) - 1)
        length = sizeof(entry->name) - 1;

    entry->name_length = (uint8_t)length;

    /*
     * NBFS directory type 2 = directory.
     */
    entry->type = 2;

    /*
     * Every entry currently occupies the complete
     * fixed-size directory record.
     */
    entry->record_length =
        (uint16_t)sizeof(nbfs_dirent_t);

    memcpy(
        entry->name,
        name,
        length
    );
}


/*
 * Write the root directory to the data block supplied
 * by the filesystem allocator.
 */
int nbfs_write_root_directory(
    FILE *fp,
    uint64_t block
)
{
    uint8_t data[NBFS_DEFAULT_BLOCK_SIZE];

    nbfs_dirent_t *entries;

    uint64_t offset;


    /*
     * A directory block must be a normal data block.
     */
    if (block < NBFS_DATA_START)
        return -1;


    /*
     * Clear the complete 4 KiB directory block.
     */
    memset(
        data,
        0,
        sizeof(data)
    );


    /*
     * The first two entries are:
     *
     *   .
     *   ..
     */
    entries =
        (nbfs_dirent_t *)data;


    create_entry(
        &entries[0],
        1,
        "."
    );


    create_entry(
        &entries[1],
        1,
        ".."
    );


    /*
     * Convert filesystem block number to byte offset.
     */
    offset =
        block *
        (uint64_t)NBFS_DEFAULT_BLOCK_SIZE;


    if (fseek(
            fp,
            (long)offset,
            SEEK_SET
        ) != 0)
    {
        return -1;
    }


    /*
     * Write exactly one filesystem block.
     */
    if (fwrite(
            data,
            sizeof(data),
            1,
            fp
        ) != 1)
    {
        return -1;
    }


    fflush(fp);

    return 0;
}
