#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "nbfs.h"

/*
 * NBFS on-disk values are little-endian (see docs/nbfs/ON_DISK_LAYOUT.md).
 *
 * The host image tools and libnbfs produce little-endian images on
 * whichever host builds them.  The NeoBench 68060 is big-endian, so the
 * kernel must never read the on-disk structures with plain C field
 * access; every field is decoded with the little-endian readers below.
 * The parsed nbfs_superblock_t / nbfs_inode_t structs are then in host
 * byte order.
 */

static uint16_t nb_rd16(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;

    return (uint16_t)((uint16_t)b[0] |
                      ((uint16_t)b[1] << 8));
}

static uint32_t nb_rd32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;

    return (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static uint64_t nb_rd64(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;

    return (uint64_t)nb_rd32(b) |
           ((uint64_t)nb_rd32(b + 4) << 32);
}

/*
 * NBFS v1 block layout constants.
 */
#define NBFS_BLOCK_BYTES   4096
#define NBFS_INODE_SIZE    ((uint32_t)sizeof(nbfs_inode_t))
#define NBFS_DIR_HEADER    12u /* inode + record_length + name_length + type */

/* Directory entry type byte values (see mkfs directory.c). */
#define NBFS_ENTRY_FILE    1
#define NBFS_ENTRY_DIR     2

/*
 * On-disk directory entry.  Fields are little-endian, so offsets are
 * only used for decoding; the struct is never dereferenced directly.
 */
struct nbfs_directory_entry
{
    uint64_t inode;
    uint16_t record_length;
    uint8_t name_length;
    uint8_t type;
} __attribute__((packed));

static int nbfs_ready;
static nbfs_mount_t nbfs_mount;
static nbfs_superblock_t nbfs_sb;

static int nbfs_read_block(
    nbfs_mount_t *mount,
    uint64_t block,
    void *buffer)
{
    if (!mount || !mount->device || !buffer)
        return -1;

    if (block >= mount->block_count)
        return -1;

    return block_device_read(
        mount->device,
        block,
        buffer);
}

/*
 * Decode a raw on-disk superblock block into a host-order struct.
 */
static void nbfs_parse_superblock(
    const uint8_t *raw,
    nbfs_superblock_t *sb)
{
    memset(sb, 0, sizeof(*sb));

    sb->magic =
        nb_rd32(raw + offsetof(nbfs_superblock_t, magic));

    sb->version_major =
        nb_rd16(raw + offsetof(nbfs_superblock_t, version_major));

    sb->version_minor =
        nb_rd16(raw + offsetof(nbfs_superblock_t, version_minor));

    sb->block_size =
        nb_rd32(raw + offsetof(nbfs_superblock_t, block_size));

    sb->flags =
        nb_rd32(raw + offsetof(nbfs_superblock_t, flags));

    sb->total_blocks =
        nb_rd64(raw + offsetof(nbfs_superblock_t, total_blocks));

    sb->free_blocks =
        nb_rd64(raw + offsetof(nbfs_superblock_t, free_blocks));

    sb->total_inodes =
        nb_rd64(raw + offsetof(nbfs_superblock_t, total_inodes));

    sb->free_inodes =
        nb_rd64(raw + offsetof(nbfs_superblock_t, free_inodes));

    sb->root_inode =
        nb_rd64(raw + offsetof(nbfs_superblock_t, root_inode));

    sb->journal_start =
        nb_rd64(raw + offsetof(nbfs_superblock_t, journal_start));

    sb->journal_blocks =
        nb_rd64(raw + offsetof(nbfs_superblock_t, journal_blocks));

    sb->block_bitmap_start =
        nb_rd64(raw + offsetof(nbfs_superblock_t, block_bitmap_start));

    sb->inode_bitmap_start =
        nb_rd64(raw + offsetof(nbfs_superblock_t, inode_bitmap_start));

    sb->inode_table_start =
        nb_rd64(raw + offsetof(nbfs_superblock_t, inode_table_start));

    sb->data_start =
        nb_rd64(raw + offsetof(nbfs_superblock_t, data_start));

    memcpy(
        sb->volume_name,
        raw + offsetof(nbfs_superblock_t, volume_name),
        sizeof(sb->volume_name));

    sb->crc32 =
        nb_rd32(raw + offsetof(nbfs_superblock_t, crc32));
}

/*
 * Decode a raw 248-byte on-disk inode into a host-order struct.
 */
static void nbfs_parse_inode(
    const uint8_t *raw,
    nbfs_inode_t *inode)
{
    unsigned int i;

    memset(inode, 0, sizeof(*inode));

    inode->inode_number =
        nb_rd64(raw + offsetof(nbfs_inode_t, inode_number));

    inode->mode =
        nb_rd16(raw + offsetof(nbfs_inode_t, mode));

    inode->links =
        nb_rd16(raw + offsetof(nbfs_inode_t, links));

    inode->uid =
        nb_rd32(raw + offsetof(nbfs_inode_t, uid));

    inode->gid =
        nb_rd32(raw + offsetof(nbfs_inode_t, gid));

    inode->size =
        nb_rd64(raw + offsetof(nbfs_inode_t, size));

    inode->created =
        nb_rd64(raw + offsetof(nbfs_inode_t, created));

    inode->modified =
        nb_rd64(raw + offsetof(nbfs_inode_t, modified));

    inode->accessed =
        nb_rd64(raw + offsetof(nbfs_inode_t, accessed));

    for (i = 0; i < NBFS_EXTENTS_PER_INODE; i++)
    {
        inode->extents[i].start_block =
            nb_rd64(raw + offsetof(nbfs_inode_t,
                                   extents[i].start_block));

        inode->extents[i].block_count =
            nb_rd32(raw + offsetof(nbfs_inode_t,
                                   extents[i].block_count));

        inode->extents[i].flags =
            nb_rd32(raw + offsetof(nbfs_inode_t,
                                   extents[i].flags));
    }

    inode->crc32 =
        nb_rd32(raw + offsetof(nbfs_inode_t, crc32));
}

/*
 * Read one contiguous raw inode (248 bytes) from the inode table,
 * even when it straddles a block boundary.
 */
static int nbfs_read_raw_inode(
    nbfs_mount_t *mount,
    uint64_t inode_number,
    uint8_t *raw)
{
    uint64_t index;
    uint64_t byte_offset;
    uint64_t block;
    uint32_t offset;
    uint8_t block_buffer[NBFS_BLOCK_BYTES];

    if (inode_number == 0 || inode_number > mount->inode_count)
        return -1;

    if (mount->block_size == 0 ||
        mount->block_size > sizeof(block_buffer))
        return -1;

    /*
     * NBFS inode numbers are one-based.
     */
    index = inode_number - 1;

    byte_offset = index * (uint64_t)NBFS_INODE_SIZE;

    if (byte_offset / mount->block_size >=
        mount->inode_table_blocks)
        return -1;

    block = mount->inode_table_start +
            (byte_offset / mount->block_size);

    offset = (uint32_t)(byte_offset % mount->block_size);

    /*
     * Normal case: the inode fits entirely in one block.
     */
    if (offset + NBFS_INODE_SIZE <= mount->block_size)
    {
        if (nbfs_read_block(
                mount,
                block,
                block_buffer) != 0)
            return -1;

        memcpy(raw, block_buffer + offset, NBFS_INODE_SIZE);

        return 0;
    }

    /*
     * Inode crosses a block boundary: gather both halves.
     */
    {
        uint32_t first  = mount->block_size - offset;
        uint32_t second = NBFS_INODE_SIZE - first;

        if (second > mount->block_size)
            return -1;

        if (nbfs_read_block(
                mount,
                block,
                block_buffer) != 0)
            return -1;

        memcpy(raw, block_buffer + offset, first);

        if (nbfs_read_block(
                mount,
                block + 1,
                block_buffer) != 0)
            return -1;

        memcpy(raw + first, block_buffer, second);
    }

    return 0;
}

int nbfs_kernel_init(void)
{
    nbfs_ready = 1;

    memset(&nbfs_mount, 0, sizeof(nbfs_mount));
    memset(&nbfs_sb, 0, sizeof(nbfs_sb));

    return 0;
}

int nbfs_kernel_probe(block_device_t *device)
{
    uint8_t raw[NBFS_BLOCK_BYTES];
    nbfs_superblock_t sb;

    if (!device || !device->read || device->block_size == 0)
        return -1;

    if (block_device_read(
            device,
            NBFS_SUPERBLOCK,
            raw) != 0)
        return -1;

    nbfs_parse_superblock(raw, &sb);

    if (sb.magic != NBFS_MAGIC)
        return -1;

    if (sb.version_major != NBFS_VERSION_MAJOR)
        return -1;

    if (sb.block_size != device->block_size)
        return -1;

    if (sb.total_blocks == 0)
        return -1;

    return 0;
}

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device)
{
    uint8_t raw[NBFS_BLOCK_BYTES];
    nbfs_superblock_t sb;

    if (!fs || !device || !nbfs_ready)
        return -1;

    if (!device->read || device->block_size == 0)
        return -1;

    /*
     * NBFS superblock lives at block 1.
     */
    if (block_device_read(
            device,
            NBFS_SUPERBLOCK,
            raw) != 0)
        return -1;

    nbfs_parse_superblock(raw, &sb);

    if (sb.magic != NBFS_MAGIC)
        return -1;

    if (sb.version_major != NBFS_VERSION_MAJOR)
        return -1;

    if (sb.block_size != device->block_size)
        return -1;

    if (sb.block_size != NBFS_BLOCK_BYTES)
        return -1;

    if (sb.total_blocks == 0 ||
        sb.total_blocks > device->block_count)
        return -1;

    if (sb.root_inode == 0 ||
        sb.root_inode > sb.total_inodes)
        return -1;

    if (sb.inode_table_start == 0 ||
        sb.data_start <= sb.inode_table_start)
        return -1;

    memset(&nbfs_mount, 0, sizeof(nbfs_mount));

    nbfs_mount.device = device;
    nbfs_mount.block_size = sb.block_size;
    nbfs_mount.block_count = sb.total_blocks;
    nbfs_mount.root_inode = sb.root_inode;
    nbfs_mount.inode_count = sb.total_inodes;
    nbfs_mount.inode_table_start = sb.inode_table_start;
    nbfs_mount.inode_table_blocks = NBFS_INODE_TABLE_BLOCKS;
    nbfs_mount.data_start = sb.data_start;
    nbfs_mount.mounted = 1;

    nbfs_sb = sb;

    fs->block_size = sb.block_size;
    fs->private_data = &nbfs_mount;
    fs->lookup = nbfs_kernel_lookup;
    fs->get_size = nbfs_kernel_get_size;
    fs->read_file = nbfs_kernel_read;

    return 0;
}

int nbfs_kernel_get_size(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t *size)
{
    nbfs_inode_t inode;

    if (!fs || !size)
        return -1;

    if (nbfs_kernel_read_inode(
            fs,
            inode_number,
            &inode) != 0)
        return -1;

    *size = inode.size;

    return 0;
}

int nbfs_kernel_read_inode(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    nbfs_inode_t *inode)
{
    nbfs_mount_t *mount;
    uint8_t raw[NBFS_INODE_SIZE];

    if (!fs || !inode)
        return -1;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted || !mount->device)
        return -1;

    if (nbfs_read_raw_inode(
            mount,
            inode_number,
            raw) != 0)
        return -1;

    nbfs_parse_inode(raw, inode);

    return 0;
}

ssize_t nbfs_kernel_read(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    nbfs_mount_t *mount;
    nbfs_inode_t inode;

    uint64_t remaining;
    uint64_t position;
    uint8_t *output;

    uint8_t block_buffer[NBFS_BLOCK_BYTES];

    if (!fs || !buffer)
        return -1;

    if (size == 0)
        return 0;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted || !mount->device)
        return -1;

    if (mount->block_size == 0 ||
        mount->block_size > sizeof(block_buffer))
        return -1;

    if (nbfs_kernel_read_inode(
            fs,
            inode_number,
            &inode) != 0)
        return -1;

    if (offset >= inode.size)
        return 0;

    remaining = inode.size - offset;

    if ((uint64_t)size < remaining)
        remaining = (uint64_t)size;

    position = offset;
    output = (uint8_t *)buffer;

    /*
     * Walk the inode extents.
     */
    for (unsigned int i = 0;
         i < NBFS_EXTENTS_PER_INODE && remaining > 0;
         i++)
    {
        nbfs_extent_t *extent = &inode.extents[i];

        uint64_t extent_bytes;
        uint64_t extent_start_byte;
        uint64_t extent_end_byte;

        if (extent->block_count == 0)
            continue;

        extent_bytes =
            (uint64_t)extent->block_count *
            mount->block_size;

        extent_start_byte = 0;

        for (unsigned int j = 0; j < i; j++)
        {
            nbfs_extent_t *previous = &inode.extents[j];

            extent_start_byte +=
                (uint64_t)previous->block_count *
                mount->block_size;
        }

        extent_end_byte =
            extent_start_byte + extent_bytes;

        if (position >= extent_end_byte)
            continue;

        {
            uint64_t local =
                position > extent_start_byte
                    ? position - extent_start_byte
                    : 0;

            uint64_t available =
                extent_end_byte -
                (extent_start_byte + local);

            uint64_t to_copy =
                remaining < available
                    ? remaining
                    : available;

            while (to_copy > 0)
            {
                uint64_t logical_block =
                    local / mount->block_size;

                uint32_t block_offset =
                    (uint32_t)(local % mount->block_size);

                uint64_t physical_block =
                    extent->start_block +
                    logical_block;

                uint64_t chunk =
                    mount->block_size - block_offset;

                if (chunk > to_copy)
                    chunk = to_copy;

                if (block_offset == 0 &&
                    chunk == mount->block_size)
                {
                    if (nbfs_read_block(
                            mount,
                            physical_block,
                            output) != 0)
                        return -1;
                }
                else
                {
                    if (nbfs_read_block(
                            mount,
                            physical_block,
                            block_buffer) != 0)
                        return -1;

                    memcpy(
                        output,
                        block_buffer + block_offset,
                        (size_t)chunk);
                }

                output += chunk;
                position += chunk;
                remaining -= chunk;
                local += chunk;
                to_copy -= chunk;
            }
        }
    }

    return (ssize_t)((uint8_t *)output - (uint8_t *)buffer);
}

/*
 * Scan a directory inode for name, returning the child inode number
 * and the child's on-disk mode (NBFS_MODE_DIR / NBFS_MODE_FILE).
 */
static int nbfs_directory_find(
    vfs_filesystem_t *fs,
    uint64_t directory_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode)
{
    nbfs_inode_t dir;
    uint64_t dir_size;
    uint64_t position;
    size_t name_len;
    uint8_t block[NBFS_BLOCK_BYTES];

    if (!fs || !name || !result_inode || !result_mode)
        return -1;

    name_len = strlen(name);

    if (name_len == 0 || name_len > NBFS_MAX_NAME_LENGTH)
        return -1;

    if (nbfs_kernel_read_inode(
            fs,
            directory_inode,
            &dir) != 0)
        return -1;

    if ((dir.mode & 0xF000) != NBFS_MODE_DIRECTORY)
        return -1;

    dir_size = dir.size;
    position = 0;

    while (position < dir_size)
    {
        ssize_t got;
        size_t amount;

        got = nbfs_kernel_read(
            fs,
            directory_inode,
            position,
            block,
            sizeof(block));

        if (got <= 0)
            return -1;

        amount = (size_t)got;
        position += amount;

        {
            size_t offset = 0;

            while (offset + NBFS_DIR_HEADER <= amount)
            {
                uint8_t *entry = block + offset;
                uint16_t record_length;
                uint8_t name_length;
                uint64_t inode_number;
                const char *entry_name;

                record_length =
                    nb_rd16(entry +
                            offsetof(struct nbfs_directory_entry,
                                     record_length));

                if (record_length == 0)
                    break;

                if (record_length < NBFS_DIR_HEADER)
                    return -1;

                if (offset + record_length > amount)
                    return -1;

                inode_number =
                    nb_rd64(entry +
                            offsetof(struct nbfs_directory_entry,
                                     inode));

                name_length =
                    *(const uint8_t *)(entry +
                        offsetof(struct nbfs_directory_entry,
                                 name_length));

                if (inode_number != 0 &&
                    name_length == name_len &&
                    name_length <=
                        record_length - NBFS_DIR_HEADER)
                {
                    entry_name =
                        (const char *)(entry + NBFS_DIR_HEADER);

                    if (memcmp(
                            entry_name,
                            name,
                            name_len) == 0)
                    {
                        nbfs_inode_t child;

                        if (nbfs_kernel_read_inode(
                                fs,
                                inode_number,
                                &child) != 0)
                            return -1;

                        *result_inode = inode_number;
                        *result_mode = (uint32_t)child.mode;

                        return 0;
                    }
                }

                offset += record_length;
            }
        }
    }

    return -1;
}

int nbfs_kernel_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode)
{
    nbfs_mount_t *mount;

    if (!fs || !name || !result_inode || !result_mode)
        return -1;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted)
        return -1;

    return nbfs_directory_find(
        fs,
        parent_inode,
        name,
        result_inode,
        result_mode);
}

int nbfs_kernel_root_inode(
    const vfs_filesystem_t *fs,
    uint64_t *root_inode)
{
    nbfs_mount_t *mount;

    if (!fs || !root_inode)
        return -1;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted)
        return -1;

    *root_inode = mount->root_inode;

    return 0;
}

int nbfs_kernel_volume(
    const vfs_filesystem_t *fs,
    char *out,
    size_t out_size)
{
    nbfs_mount_t *mount;
    size_t name_len;

    if (!fs || !out || out_size == 0)
        return -1;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted)
        return -1;

    name_len = strlen(nbfs_sb.volume_name);

    if (name_len >= out_size)
        name_len = out_size - 1;

    memcpy(out, nbfs_sb.volume_name, name_len);
    out[name_len] = '\0';

    return 0;
}

void nbfs_kernel_shutdown(void)
{
    memset(&nbfs_sb, 0, sizeof(nbfs_sb));
    memset(&nbfs_mount, 0, sizeof(nbfs_mount));
    nbfs_ready = 0;
}