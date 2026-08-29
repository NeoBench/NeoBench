/*
 * NeoBench File System (NBFS) -- Block I/O layer.
 *
 * Reads on-disk structures through the FreeBSD buffer cache.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/endian.h>
#include <sys/vnode.h>

#include "nbfs.h"

/*
 * Read a single block from the NBFS volume through the buffer cache.
 * The caller provides a block-sized buffer (NBFS_BLOCK_SIZE bytes).
 */
int
nbfs_read_block(struct nbfs_mount *nmp, uint64_t blkno, void *buf)
{
	struct vnode *vp;
	struct buf *bp;
	daddr_t dbn;
	int error;

	KASSERT(nmp != NULL, ("nbfs_read_block: NULL mount"));
	KASSERT(buf != NULL, ("nbfs_read_block: NULL buffer"));
	KASSERT(nmp->nm_mountp != NULL, ("nbfs_read_block: NULL mountp"));

	vp = nmp->nm_mountp->mnt_vnodecovered;
	if (vp == NULL)
		vp = nmp->nm_rootvp;
	if (vp == NULL)
		return (ENXIO);

	dbn = (daddr_t)blkno;
	error = bread(vp, dbn, NBFS_BLOCK_SIZE, NULL, &bp);
	if (error != 0) {
		brelse(bp);
		return (error);
	}

	memcpy(buf, bp->b_data, NBFS_BLOCK_SIZE);
	brelse(bp);
	return (0);
}

/*
 * Read an NBFS inode from the on-disk inode table.
 * Inodes are 1-based; the on-disk offset is computed from the inode table
 * start block stored in the superblock.
 */
int
nbfs_read_inode(struct nbfs_mount *nmp, uint64_t ino,
    struct nbfs_disk_inode *dip)
{
	uint64_t idx, byte_offset, blkno;
	uint32_t blkoff;
	uint8_t block[NBFS_BLOCK_SIZE];
	int error;

	KASSERT(nmp != NULL, ("nbfs_read_inode: NULL mount"));
	KASSERT(dip != NULL, ("nbfs_read_inode: NULL inode"));

	if (ino == 0 || ino < 1)
		return (EINVAL);

	/* Inodes are 1-based; index is 0-based. */
	idx = ino - 1;
	byte_offset = idx * sizeof(struct nbfs_disk_inode);

	/* Check bounds: the inode must fit within the inode table. */
	if (byte_offset / NBFS_BLOCK_SIZE >= NBFS_BLKO_INODE_TABLE_BLOCKS)
		return (EINVAL);

	blkno = nmp->nm_super.ds_inode_table_start +
	    (byte_offset / NBFS_BLOCK_SIZE);
	blkoff = (uint32_t)(byte_offset % NBFS_BLOCK_SIZE);

	error = nbfs_read_block(nmp, blkno, block);
	if (error != 0)
		return (error);

	/* Check that the full inode fits in this block. */
	if (blkoff + sizeof(struct nbfs_disk_inode) > NBFS_BLOCK_SIZE) {
		/*
		 * Inode spans a block boundary -- read the second half.
		 * For NBFS v1 with 4K blocks and ~100-byte inodes this
		 * should never happen, but handle it defensively.
		 */
		uint8_t block2[NBFS_BLOCK_SIZE];
		uint32_t first, second;

		first = NBFS_BLOCK_SIZE - blkoff;
		second = sizeof(struct nbfs_disk_inode) - first;

		memcpy(dip, block + blkoff, first);

		error = nbfs_read_block(nmp, blkno + 1, block2);
		if (error != 0)
			return (error);

		memcpy((uint8_t *)dip + first, block2, second);
	} else {
		memcpy(dip, block + blkoff, sizeof(struct nbfs_disk_inode));
	}

	/* Byte-swap on-disk fields to host order. */
	dip->di_inode_number = be64toh(dip->di_inode_number);
	dip->di_mode = be16toh(dip->di_mode);
	dip->di_links = be16toh(dip->di_links);
	dip->di_uid = be32toh(dip->di_uid);
	dip->di_gid = be32toh(dip->di_gid);
	dip->di_size = be64toh(dip->di_size);
	dip->di_created = be64toh(dip->di_created);
	dip->di_modified = be64toh(dip->di_modified);
	dip->di_accessed = be64toh(dip->di_accessed);
	dip->di_crc32 = be32toh(dip->di_crc32);

	for (int i = 0; i < NBFS_EXTENTS_PER_INODE; i++) {
		dip->di_extents[i].de_start_block =
		    be64toh(dip->di_extents[i].de_start_block);
		dip->di_extents[i].de_block_count =
		    be32toh(dip->di_extents[i].de_block_count);
		dip->di_extents[i].de_flags =
		    be32toh(dip->di_extents[i].de_flags);
	}

	return (0);
}

/*
 * Read and validate the NBFS superblock from block 1.
 * Populates the in-core mount structure with host-byte-order values.
 */
int
nbfs_read_superblock(struct nbfs_mount *nmp)
{
	uint8_t block[NBFS_BLOCK_SIZE];
	struct nbfs_disk_superblock *ds;
	int error;

	KASSERT(nmp != NULL, ("nbfs_read_superblock: NULL mount"));

	error = nbfs_read_block(nmp, NBFS_BLKO_SUPERBLOCK, block);
	if (error != 0)
		return (error);

	ds = (struct nbfs_disk_superblock *)block;

	/* Validate magic. */
	if (be32toh(ds->ds_magic) != NBFS_MAGIC) {
		printf("nbfs: bad magic 0x%08x (expected 0x%08x)\n",
		    be32toh(ds->ds_magic), NBFS_MAGIC);
		return (EFTYPE);
	}

	/* Validate version. */
	if (be16toh(ds->ds_version_major) != NBFS_VERSION_MAJOR) {
		printf("nbfs: unsupported version %d.%d\n",
		    be16toh(ds->ds_version_major),
		    be16toh(ds->ds_version_minor));
		return (EFTYPE);
	}

	/* Validate block size. */
	if (be32toh(ds->ds_block_size) != NBFS_BLOCK_SIZE) {
		printf("nbfs: unsupported block size %u\n",
		    be32toh(ds->ds_block_size));
		return (EFTYPE);
	}

	/* Copy to mount structure and byte-swap. */
	memcpy(&nmp->nm_super, ds, sizeof(*ds));
	nmp->nm_super.ds_magic = be32toh(nmp->nm_super.ds_magic);
	nmp->nm_super.ds_version_major =
	    be16toh(nmp->nm_super.ds_version_major);
	nmp->nm_super.ds_version_minor =
	    be16toh(nmp->nm_super.ds_version_minor);
	nmp->nm_super.ds_block_size = be32toh(nmp->nm_super.ds_block_size);
	nmp->nm_super.ds_flags = be32toh(nmp->nm_super.ds_flags);
	nmp->nm_super.ds_total_blocks =
	    be64toh(nmp->nm_super.ds_total_blocks);
	nmp->nm_super.ds_free_blocks =
	    be64toh(nmp->nm_super.ds_free_blocks);
	nmp->nm_super.ds_total_inodes =
	    be64toh(nmp->nm_super.ds_total_inodes);
	nmp->nm_super.ds_free_inodes =
	    be64toh(nmp->nm_super.ds_free_inodes);
	nmp->nm_super.ds_root_inode = be64toh(nmp->nm_super.ds_root_inode);
	nmp->nm_super.ds_journal_start =
	    be64toh(nmp->nm_super.ds_journal_start);
	nmp->nm_super.ds_journal_blocks =
	    be64toh(nmp->nm_super.ds_journal_blocks);
	nmp->nm_super.ds_block_bitmap_start =
	    be64toh(nmp->nm_super.ds_block_bitmap_start);
	nmp->nm_super.ds_inode_bitmap_start =
	    be64toh(nmp->nm_super.ds_inode_bitmap_start);
	nmp->nm_super.ds_inode_table_start =
	    be64toh(nmp->nm_super.ds_inode_table_start);
	nmp->nm_super.ds_data_start =
	    be64toh(nmp->nm_super.ds_data_start);
	nmp->nm_super.ds_crc32 = be32toh(nmp->nm_super.ds_crc32);

	/* Populate mount convenience fields. */
	nmp->nm_blocksize = nmp->nm_super.ds_block_size;
	nmp->nm_totalblocks = nmp->nm_super.ds_total_blocks;
	nmp->nm_freeblocks = nmp->nm_super.ds_free_blocks;
	nmp->nm_totalinodes = nmp->nm_super.ds_total_inodes;
	nmp->nm_freeinodes = nmp->nm_super.ds_free_inodes;
	nmp->nm_rootinode = nmp->nm_super.ds_root_inode;
	nmp->nm_datastart = nmp->nm_super.ds_data_start;
	memcpy(nmp->nm_volname, nmp->nm_super.ds_volume_name,
	    sizeof(nmp->nm_volname));

	return (0);
}
