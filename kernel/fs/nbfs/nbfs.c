#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "nbfs.h"

static int nbfs_ready;
static nbfs_mount_t nbfs_mount;

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

int nbfs_kernel_init(void)
{
    nbfs_ready = 1;

    memset(&nbfs_mount, 0, sizeof(nbfs_mount));

    return 0;
}

int nbfs_kernel_mount(
    vfs_filesystem_t *fs,
    block_device_t *device
)
{
    nbfs_superblock_t superblock;

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
            &superblock) != 0)
        return -1;

    if (superblock.magic != NBFS_MAGIC)
        return -1;

    if (superblock.version_major != NBFS_VERSION_MAJOR)
        return -1;

    if (superblock.block_size != device->block_size)
        return -1;

    if (superblock.total_blocks == 0 ||
        superblock.inode_table_start == 0)
        return -1;

    memset(&nbfs_mount, 0, sizeof(nbfs_mount));

    nbfs_mount.device = device;
    nbfs_mount.block_size = superblock.block_size;
    nbfs_mount.block_count = superblock.total_blocks;
    nbfs_mount.root_inode = superblock.root_inode;
    nbfs_mount.inode_count = superblock.total_inodes;
    nbfs_mount.inode_table_start =
        superblock.inode_table_start;
    nbfs_mount.inode_table_blocks =
        NBFS_INODE_TABLE_BLOCKS;
    nbfs_mount.data_start =
        superblock.data_start;
    nbfs_mount.mounted = 1;

    fs->block_size = superblock.block_size;
    fs->private_data = &nbfs_mount;
    fs->lookup = nbfs_kernel_lookup;

    return 0;
}

int nbfs_kernel_read_inode(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    nbfs_inode_t *inode
)
{
    nbfs_mount_t *mount;
    uint64_t index;
    uint64_t byte_offset;
    uint64_t block;
    uint32_t offset;
    uint32_t inode_size;
    uint8_t block_buffer[4096];

    if (!fs || !inode)
        return -1;

    mount = (nbfs_mount_t *)fs->private_data;

    if (!mount || !mount->mounted || !mount->device)
        return -1;

    if (inode_number == 0)
        return -1;

    if (inode_number > mount->inode_count)
        return -1;

    inode_size = (uint32_t)sizeof(nbfs_inode_t);

    if (mount->block_size == 0 ||
        mount->block_size > sizeof(block_buffer))
        return -1;

    /*
     * NBFS inode numbers are one-based.
     */
    index = inode_number - 1;

    byte_offset = index * (uint64_t)inode_size;

    if (byte_offset / mount->block_size >=
        mount->inode_table_blocks)
        return -1;

    block = mount->inode_table_start +
            (byte_offset / mount->block_size);

    offset = (uint32_t)(
        byte_offset % mount->block_size);

    /*
     * Normal case: inode fits entirely in one block.
     */
    if (offset + inode_size <= mount->block_size)
    {
        if (nbfs_read_block(
                mount,
                block,
                block_buffer) != 0)
            return -1;

        memcpy(
            inode,
            block_buffer + offset,
            inode_size);

        return 0;
    }

    /*
     * Defensive handling for an inode crossing a block boundary.
     */
    {
        uint32_t first;
        uint32_t second;

        first = mount->block_size - offset;
        second = inode_size - first;

        if (second > mount->block_size)
            return -1;

        if (nbfs_read_block(
                mount,
                block,
                block_buffer) != 0)
            return -1;

        memcpy(
            inode,
            block_buffer + offset,
            first);

        if (nbfs_read_block(
                mount,
                block + 1,
                block_buffer) != 0)
            return -1;

        memcpy(
            ((uint8_t *)inode) + first,
            block_buffer,
            second);
    }

    return 0;
}

ssize_t nbfs_kernel_read(
    vfs_filesystem_t *fs,
    uint64_t inode_number,
    uint64_t offset,
    void *buffer,
    size_t size
)
{
    nbfs_mount_t *mount;
    nbfs_inode_t inode;

    uint64_t remaining;
    uint64_t position;
    uint8_t *output;

    uint8_t block_buffer[4096];

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

        /*
         * Determine the logical byte position of this extent.
         */
        for (unsigned int j = 0; j < i; j++)
        {
            nbfs_extent_t *previous = &inode.extents[j];

            extent_start_byte +=
                (uint64_t)previous->block_count *
                mount->block_size;
        }

        extent_end_byte =
            extent_start_byte + extent_bytes;

        /*
         * This extent is before the requested position.
         */
        if (position >= extent_end_byte)
            continue;

        /*
         * Requested position is inside this extent.
         */
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
                    (uint32_t)(
                        local % mount->block_size);

                uint64_t physical_block =
                    extent->start_block +
                    logical_block;

                uint64_t chunk =
                    mount->block_size -
                    block_offset;

                if (chunk > to_copy)
                    chunk = to_copy;

                /*
                 * Full-block reads can go directly into the
                 * caller's buffer.
                 */
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
            }
        }
    }

    return (ssize_t)((uint8_t *)output -
                     (uint8_t *)buffer);
}

int nbfs_kernel_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    unsigned long *result_type
)
{
    if (!fs || !name ||
        !result_inode || !result_type)
        return -1;

    if (!nbfs_mount.mounted)
        return -1;

    /*
     * Directory lookup remains the next NBFS layer.
     */
    (void)parent_inode;

    *result_inode = 0;
    *result_type = 0;

    return -1;
}

void nbfs_kernel_shutdown(void)
{
    memset(&nbfs_mount, 0, sizeof(nbfs_mount));
    nbfs_ready = 0;
}
