#include "vol.h"
#include <string.h>

#define NBH_RD16(p)        nbh_rd16((const uint8_t *)(p))
#define NBH_RD32(p)        nbh_rd32((const uint8_t *)(p))
#define NBH_RD64(p)        nbh_rd64((const uint8_t *)(p))

static int rd_block(struct nbh_vol *v, uint32_t block, uint8_t *buf)
{
    if (block >= v->total_blocks)
        return -1;
    return v->io.read(v->io.ud, block * v->sectors_per_block,
                      v->sectors_per_block, buf);
}

#define NBFS_SUPERBLOCK  1u

#define SB_MAGIC       0u
#define SB_VMAJ        4u
#define SB_VMIN        6u
#define SB_BS          8u
#define SB_FLAGS       12u
#define SB_TOTAL       16u
#define SB_FREE        24u
#define SB_TINODES     32u
#define SB_FINODES     40u
#define SB_ROOT        48u
#define SB_JSTART      56u
#define SB_JBLOCKS     64u
#define SB_BMP         72u
#define SB_IBMP        80u
#define SB_ITABLE      88u
#define SB_DATA        96u
#define SB_VOLNAME     104u

int nbh_mount(struct nbh_vol *v)
{
    uint8_t sb[NBH_BLOCK_SIZE];
    struct nbh_io io = v->io;
    uint64_t magic;
    uint32_t bs;

    memset(v, 0, sizeof(*v));
    v->io = io;
    v->sector_size = NBH_SECTOR_SIZE;
    v->block_size = NBH_BLOCK_SIZE;
    v->sectors_per_block = NBH_SECTORS_PER_BLOCK;
    v->inode_blocks = NBH_INODE_TABLE_BLOCKS;

    if (v->io.read(v->io.ud, NBFS_SUPERBLOCK * v->sectors_per_block,
                   v->sectors_per_block, sb) != 0)
        return -1;

    magic = NBH_RD32(sb + SB_MAGIC);
    if (magic != NBH_MAGIC)
        return -1;
    if (NBH_RD16(sb + SB_VMAJ) != NBH_VERSION)
        return -1;
    bs = NBH_RD32(sb + SB_BS);
    if (bs != NBH_BLOCK_SIZE)
        return -1;

    v->total_blocks = (uint32_t)NBH_RD64(sb + SB_TOTAL);
    v->free_blocks = (uint32_t)NBH_RD64(sb + SB_FREE);
    v->total_inodes = (uint32_t)NBH_RD64(sb + SB_TINODES);
    v->free_inodes = (uint32_t)NBH_RD64(sb + SB_FINODES);
    v->root_inode = (uint32_t)NBH_RD64(sb + SB_ROOT);
    v->itable_start = (uint32_t)NBH_RD64(sb + SB_ITABLE);
    v->data_start = (uint32_t)NBH_RD64(sb + SB_DATA);

    {
        const uint8_t *nm = sb + SB_VOLNAME;
        unsigned int i;
        for (i = 0; i < 63; i++)
        {
            v->volname[i] = (char)nm[i];
            if (nm[i] == 0)
                break;
        }
        v->volname[63] = 0;
    }

    if (v->root_inode == 0 || v->root_inode >= NBH_MAX_INODES)
        return -1;
    if (v->itable_start == 0 || v->itable_start >= v->total_blocks)
        return -1;
    if (v->total_blocks == 0)
        return -1;

    return 0;
}

int nbh_read_inode(struct nbh_vol *v, uint32_t inode, struct nbh_inode *out)
{
    uint64_t index;
    uint32_t ino_block;
    uint32_t ino_off;

    if (inode == 0 || inode > NBH_MAX_INODES)
        return -1;

    /* NBFS inode numbers are one-based: slot index = inode - 1. */
    index = (uint64_t)inode - 1;
    ino_block = (uint32_t)(index / NBH_INODES_PER_BLOCK);
    ino_off = (uint32_t)((index % NBH_INODES_PER_BLOCK) * NBH_INODE_SIZE);

    if (rd_block(v, v->itable_start + ino_block, v->inode_buf) != 0)
        return -1;

    out->mode = NBH_RD16(v->inode_buf + ino_off + NBH_INO_OFF_MODE);
    out->size = NBH_RD64(v->inode_buf + ino_off + NBH_INO_OFF_SIZE);

    {
        unsigned int i;
        for (i = 0; i < NBH_EXTENTS_PER_INODE; i++)
        {
            uint32_t off = ino_off + NBH_INO_OFF_EXTENTS + i * NBH_INO_EXTENT_STRIDE;
            out->extents[i].start_block = NBH_RD64(v->inode_buf + off + 0);
            out->extents[i].block_count = NBH_RD32(v->inode_buf + off + 8);
            out->extents[i].flags = NBH_RD32(v->inode_buf + off + 12);
        }
    }

    return 0;
}

int nbh_read(struct nbh_vol *v, const struct nbh_inode *e,
             uint64_t offset, void *buf, uint32_t size)
{
    uint8_t *out = (uint8_t *)buf;
    uint32_t fsize;
    uint32_t remaining;
    uint32_t position;

    if (size == 0)
        return 0;
    fsize = e->size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)e->size;
    if (offset >= fsize)
        return 0;

    remaining = fsize - (uint32_t)offset;
    if (size < remaining)
        remaining = size;

    position = (uint32_t)offset;

    {
        unsigned int i;
        for (i = 0; i < NBH_EXTENTS_PER_INODE && remaining > 0; i++)
        {
            const struct nbh_extent *extent = &e->extents[i];
            uint32_t extent_bytes;
            uint32_t extent_start_byte;
            uint32_t extent_end_byte;

            if (extent->block_count == 0)
                continue;

            extent_bytes = extent->block_count * v->block_size;
            extent_start_byte = 0;

            {
                unsigned int j;
                for (j = 0; j < i; j++)
                    extent_start_byte += e->extents[j].block_count *
                                         v->block_size;
            }

            extent_end_byte = extent_start_byte + extent_bytes;
            if (position >= extent_end_byte)
                continue;

            {
                uint32_t local = position > extent_start_byte
                                     ? position - extent_start_byte
                                     : 0;
                uint32_t available = extent_end_byte -
                                     (extent_start_byte + local);
                uint32_t to_copy = remaining < available
                                       ? remaining
                                       : available;

                while (to_copy > 0)
                {
                    uint32_t logical_block = local / v->block_size;
                    uint32_t block_offset = local % v->block_size;
                    uint32_t physical_block =
                        extent->start_block + logical_block;
                    uint32_t chunk = v->block_size - block_offset;
                    uint8_t tmp[NBH_BLOCK_SIZE];

                    if (chunk > to_copy)
                        chunk = to_copy;

                    if (rd_block(v, physical_block, tmp) != 0)
                        return -1;

                    memcpy(out, tmp + block_offset, (size_t)chunk);

                    out += chunk;
                    position += chunk;
                    remaining -= chunk;
                    local += chunk;
                    to_copy -= chunk;
                }
            }
        }
    }

    return (int)(position - (uint32_t)offset);
}

static int dir_hdr(struct nbh_vol *v, const struct nbh_inode *dir,
                   uint64_t off, uint8_t hdr[16])
{
    if (off + 16 > dir->size)
        return 0;
    return nbh_read(v, dir, off, hdr, 16) == 16;
}

static void dir_name(struct nbh_vol *v, const struct nbh_inode *dir,
                     uint64_t off, uint8_t nlen, char *name, uint32_t name_size)
{
    uint8_t buf[NBH_DIRENTRY_MAXNAME];
    uint32_t want = nlen;
    uint32_t n;

    if (want > NBH_DIRENTRY_MAXNAME - 1)
        want = NBH_DIRENTRY_MAXNAME - 1;
    if (nbh_read(v, dir, off + 12, buf, want) != (int)want)
        want = 0;
    n = want < name_size - 1 ? want : name_size - 1;
    memcpy(name, buf, n);
    name[n] = 0;
}

int nbh_direnum(struct nbh_vol *v, uint32_t dir_inode, uint32_t ordinal,
                struct nbh_ent *out, char *name, uint32_t name_size)
{
    struct nbh_inode dir;
    uint8_t hdr[16];
    uint64_t pos;
    uint32_t seen = 0;
    int found = 0;

    if (nbh_read_inode(v, dir_inode, &dir) != 0)
        return 0;

    pos = 0;
    while (dir_hdr(v, &dir, pos, hdr))
    {
        uint64_t ino = NBH_RD64(hdr + 0);
        uint16_t reclen = NBH_RD16(hdr + 8);
        uint8_t nlen = hdr[10];
        uint8_t type = hdr[11];

        if (ino == 0)
            break;
        if (reclen == 0)
            break;

        {
            char nm[NBH_DIRENTRY_MAXNAME];

            dir_name(v, &dir, pos, nlen, nm, sizeof(nm));
            if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
            {
                pos += reclen;
                continue;
            }
            if (seen == ordinal)
            {
                memcpy(name, nm, strlen(nm) + 1);
                out->inode = (uint32_t)ino;
                out->type = type;
                found = 1;
                break;
            }
            seen++;
        }

        pos += reclen;
    }

    return found;
}

int nbh_dirfind(struct nbh_vol *v, uint32_t dir_inode, const char *name,
                struct nbh_ent *out)
{
    struct nbh_inode dir;
    uint8_t hdr[16];
    uint64_t pos;

    if (nbh_read_inode(v, dir_inode, &dir) != 0)
        return 0;

    pos = 0;
    while (dir_hdr(v, &dir, pos, hdr))
    {
        uint64_t ino = NBH_RD64(hdr + 0);
        uint16_t reclen = NBH_RD16(hdr + 8);
        uint8_t nlen = hdr[10];

        if (ino == 0)
            break;
        if (reclen == 0)
            break;

        {
            char nm[NBH_DIRENTRY_MAXNAME];

            dir_name(v, &dir, pos, nlen, nm, sizeof(nm));
            if (strcmp(nm, name) == 0)
            {
                out->inode = (uint32_t)ino;
                out->type = hdr[11];
                return 1;
            }
        }

        pos += reclen;
    }

    return 0;
}

int nbh_volstat(struct nbh_vol *v, uint32_t *num_blocks, uint32_t *num_used,
                uint32_t *bytes_per_block)
{
    if (num_blocks)
        *num_blocks = v->total_blocks;
    if (num_used)
        *num_used = v->total_blocks > v->free_blocks
                        ? v->total_blocks - v->free_blocks
                        : 0;
    if (bytes_per_block)
        *bytes_per_block = v->block_size;
    return 0;
}