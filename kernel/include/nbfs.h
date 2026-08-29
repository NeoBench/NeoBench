#ifndef NEO_BENCH_NBFS_H
#define NEO_BENCH_NBFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include "block/device.h"
#include "vfs/filesystem.h"

/*
 * NBFS on-disk constants.
 *
 * These must remain compatible with libnbfs and the NBFS image tools.
 */
#define NBFS_MAGIC              0x5346424Eu

#define NBFS_VERSION_MAJOR      1
#define NBFS_VERSION_MINOR      0

#define NBFS_MODE_DIRECTORY     0x4000
#define NBFS_MODE_FILE          0x8000

#define NBFS_MAX_NAME_LENGTH    255
#define NBFS_EXTENTS_PER_INODE  12

#define NBFS_SUPERBLOCK         1
#define NBFS_INODE_TABLE        4
#define NBFS_INODE_TABLE_BLOCKS 64

/*
 * NBFS extent.
 */
typedef struct __attribute__((packed))
{
    uint64_t start_block;
    uint32_t block_count;
    uint32_t flags;
} nbfs_extent_t;

/*
 * NBFS inode.
 */
typedef struct __attribute__((packed))
{
    uint64_t inode_number;

    uint16_t mode;
    uint16_t links;

    uint32_t uid;
    uint32_t gid;

    uint64_t size;

    uint64_t created;
    uint64_t modified;
    uint64_t accessed;

    nbfs_extent_t extents[NBFS_EXTENTS_PER_INODE];

    uint32_t crc32;
} nbfs_inode_t;

/*
 * NBFS superblock.
 */
typedef struct __attribute__((packed))
{
    uint32_t magic;

    uint16_t version_major;
    uint16_t version_minor;

    uint32_t block_size;
    uint32_t flags;

    uint64_t total_blocks;
    uint64_t free_blocks;

    uint64_t total_inodes;
    uint64_t free_inodes;

    uint64_t root_inode;

    uint64_t journal_start;
    uint64_t journal_blocks;

    uint64_t block_bitmap_start;
    uint64_t inode_bitmap_start;
    uint64_t inode_table_start;
    uint64_t data_start;

    char volume_name[64];

    uint32_t crc32;

    uint8_t reserved[128];
} nbfs_superblock_t;

/*
 * Mounted NBFS instance.
 */
typedef struct nbfs_mount
{
    block_device_t *device;

    uint32_t block_size;

    uint64_t block_count;
    uint64_t root_inode;

    uint64_t inode_count;

    uint64_t inode_table_start;
    uint64_t inode_table_blocks;

    uint64_t data_start;

    int mounted;
} nbfs_mount_t;

/*
 * Driver lifecycle.
 */
int nbfs_kernel_init(void);

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device
);

/*
 * Validate an NBFS superblock on a device without mounting it.
 * Used at boot time to probe for a root filesystem.
 */
int nbfs_kernel_probe(
    block_device_t *device
);

/*
 * Read an inode directly from the NBFS inode table.
 */
int nbfs_kernel_read_inode(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    nbfs_inode_t *inode
);

/*
 * Read file data using the inode's extents.
 *
 * offset is the byte offset within the file.
 */
ssize_t nbfs_kernel_read(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size
);

/*
 * Report a file's size in bytes (VFS get_size operation).
 */
int nbfs_kernel_get_size(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t *size
);

/*
 * Directory lookup.
 */
int nbfs_kernel_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
);

/*
 * Read the mounted root inode number.
 */
int nbfs_kernel_root_inode(
    const vfs_filesystem_t *fs,
    uint64_t *root_inode
);

/*
 * Copy the mounted volume name into out.
 */
int nbfs_kernel_volume(
    const vfs_filesystem_t *fs,
    char *out,
    size_t out_size
);

void nbfs_kernel_shutdown(void);

#endif
