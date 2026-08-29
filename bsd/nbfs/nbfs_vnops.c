/*
 * NeoBench File System (NBFS) -- Vnode operations.
 *
 * Handles lookup, readdir, getattr, read, open, and close for NBFS
 * inodes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/endian.h>

#include "nbfs.h"

/* ------------------------------------------------------------------ */
/* Helpers.                                                            */
/* ------------------------------------------------------------------ */

/*
 * Compute the total number of data blocks occupied by an inode,
 * given its size.
 */
static uint64_t
nbfs_file_blocks(uint64_t size)
{
	if (size == 0)
		return (0);
	return ((size - 1) / NBFS_BLOCK_SIZE) + 1;
}

/*
 * Map a logical byte offset to a physical block number.
 * Returns 0 on success, -1 if the offset is beyond the file.
 */
static int
nbfs_logical_to_physical(struct nbfs_disk_inode *dip, uint64_t offset,
    uint64_t *physblk)
{
	uint64_t logblk, pos;
	unsigned int i;

	logblk = offset / NBFS_BLOCK_SIZE;
	pos = 0;

	for (i = 0; i < NBFS_EXTENTS_PER_INODE; i++) {
		uint64_t ext_blocks = dip->di_extents[i].de_block_count;

		if (ext_blocks == 0)
			continue;

		if (logblk < pos + ext_blocks) {
			*physblk = dip->di_extents[i].de_start_block +
			    (logblk - pos);
			return (0);
		}
		pos += ext_blocks;
	}

	return (-1);
}

/* ------------------------------------------------------------------ */
/* Vnode operations.                                                   */
/* ------------------------------------------------------------------ */

static int
nbfs_vop_lookup(struct vop_lookup_args *ap)
{
	struct nbfs_mount *nmp;
	struct nbfs_node *dnp;
	struct nbfs_disk_inode *dip;
	struct vnode *vp;
	struct nbfs_node *np;
	uint8_t block[NBFS_BLOCK_SIZE];
	uint64_t physblk;
	uint32_t off;
	int error;

	nmp = VFS_TO_NBFS(ap->a_dvp->v_mount);
	dnp = VNODE_TO_NBN(ap->a_dvp);
	dip = &dnp->nn_disk;

	/* Directories must have at least one extent. */
	if (dip->di_extents[0].de_block_count == 0)
		return (ENOENT);

	error = nbfs_logical_to_physical(dip, 0, &physblk);
	if (error != 0)
		return (ENOENT);

	error = nbfs_read_block(nmp, physblk, block);
	if (error != 0)
		return (error);

	/* Scan directory entries. */
	off = 0;
	while (off < NBFS_BLOCK_SIZE) {
		struct nbfs_disk_dirent *de;
		uint32_t namlen;

		de = (struct nbfs_disk_dirent *)(block + off);

		/* Zero inode means deleted entry. */
		if (de->de_inode == 0) {
			if (de->de_record_length == 0)
				break;
			off += de->de_record_length;
			continue;
		}

		namlen = de->de_name_length;
		if (namlen > NBFS_MAX_NAME)
			namlen = NBFS_MAX_NAME;

		if (namlen == ap->a_namelen &&
		    bcmp(block + off + sizeof(struct nbfs_disk_dirent),
		        ap->a_name, ap->a_namelen) == 0) {
			ino_t ino;

			ino = (ino_t)be64toh(de->de_inode);

			error = nbfs_nget(nmp, ino, &np);
			if (error != 0)
				return (error);

			vp = np->nn_vp;
			nbfs_nrele(np);

			*ap->a_vpp = vp;
			return (0);
		}

		if (de->de_record_length == 0)
			break;
		off += de->de_record_length;
	}

	return (ENOENT);
}

static int
nbfs_vop_readdir(struct vop_readdir_args *ap)
{
	struct nbfs_mount *nmp;
	struct nbfs_node *np;
	struct nbfs_disk_inode *dip;
	uint8_t block[NBFS_BLOCK_SIZE];
	uint64_t physblk;
	uint64_t cookie;
	uint32_t off;
	int numdirents;
	int error;

	nmp = VFS_TO_NBFS(ap->a_vp->v_mount);
	np = VNODE_TO_NBN(ap->a_vp);
	dip = &np->nn_disk;

	if (ap->a_vp->v_type != VDIR)
		return (ENOTDIR);

	if (dip->di_extents[0].de_block_count == 0)
		return (0);

	error = nbfs_logical_to_physical(dip, 0, &physblk);
	if (error != 0)
		return (error);

	error = nbfs_read_block(nmp, physblk, block);
	if (error != 0)
		return (error);

	cookie = ap->a_cookies[0];
	numdirents = 0;
	off = 0;

	while (off < NBFS_BLOCK_SIZE &&
	    numdirents < *ap->a_numdirents) {
		struct nbfs_disk_dirent *de;
		uint32_t namlen;
		ino_t ino;
		uint8_t dtype;

		de = (struct nbfs_disk_dirent *)(block + off);

		if (de->de_inode == 0) {
			if (de->de_record_length == 0)
				break;
			off += de->de_record_length;
			cookie++;
			continue;
		}

		if (cookie < ap->a_cookies[0]) {
			cookie++;
			if (de->de_record_length == 0)
				break;
			off += de->de_record_length;
			continue;
		}

		namlen = de->de_name_length;
		if (namlen > NBFS_MAX_NAME)
			namlen = NBFS_MAX_NAME;
		ino = (ino_t)be64toh(de->de_inode);

		if (de->de_type == NBFS_MODE_DIR)
			dtype = DT_DIR;
		else
			dtype = DT_REG;

		error = filldir(ap->a_buf,
		    (const char *)(block + off +
		        sizeof(struct nbfs_disk_dirent)),
		    namlen, cookie, ino, dtype, ap->a_ncookies);
		if (error != 0)
			break;

		numdirents++;
		cookie++;

		if (de->de_record_length == 0)
			break;
		off += de->de_record_length;
	}

	ap->a_cookies[numdirents] = cookie;
	*ap->a_numdirents = numdirents;

	return (0);
}

static int
nbfs_vop_getattr(struct vop_generic_args *ap)
{
	struct vop_getattr_args *vap;
	struct vnode *vp;
	struct nbfs_node *np;
	struct nbfs_disk_inode *dip;
	struct vattr *va;

	vap = __containerof(ap, struct vop_getattr_args, a_desc);
	vp = vap->a_vp;
	np = VNODE_TO_NBN(vp);
	dip = &np->nn_disk;
	va = vap->a_vap;

	if (vp->v_type == VDIR) {
		va->va_type = VDIR;
		va->va_mode = 0555;
	} else {
		va->va_type = VREG;
		va->va_mode = 0444;
	}

	va->va_nlink = dip->di_links;
	va->va_uid = dip->di_uid;
	va->va_gid = dip->di_gid;
	va->va_size = dip->di_size;
	va->va_bytes = nbfs_file_blocks(dip->di_size) * NBFS_BLOCK_SIZE;
	va->va_blocksize = NBFS_BLOCK_SIZE;
	va->va_filerev = 0;
	va->va_gen = 0;

	va->va_atime.tv_sec = (time_t)dip->di_accessed;
	va->va_atime.tv_nsec = 0;
	va->va_mtime.tv_sec = (time_t)dip->di_modified;
	va->va_mtime.tv_nsec = 0;
	va->va_ctime.tv_sec = (time_t)dip->di_created;
	va->va_ctime.tv_nsec = 0;

	return (0);
}

static int
nbfs_vop_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	struct nbfs_mount *nmp;
	struct nbfs_node *np;
	struct nbfs_disk_inode *dip;
	struct uio *uio;
	uint64_t filesize;
	int error;

	vp = ap->a_vp;
	nmp = VFS_TO_NBFS(vp->v_mount);
	np = VNODE_TO_NBN(vp);
	dip = &np->nn_disk;
	uio = ap->a_uio;
	filesize = dip->di_size;

	if (vp->v_type == VDIR)
		return (EISDIR);

	if (uio->uio_offset >= filesize)
		return (0);

	/* Clamp to file size. */
	if (uio->uio_offset + uio->uio_resid > filesize)
		uio->uio_resid = filesize - uio->uio_offset;

	while (uio->uio_resid > 0) {
		uint64_t offset = uio->uio_offset;
		uint64_t physblk;
		uint32_t blkoff;
		uint64_t chunk;
		uint8_t block_buf[NBFS_BLOCK_SIZE];

		error = nbfs_logical_to_physical(dip, offset, &physblk);
		if (error != 0)
			return (EIO);

		blkoff = (uint32_t)(offset % NBFS_BLOCK_SIZE);
		chunk = NBFS_BLOCK_SIZE - blkoff;
		if (chunk > (uint64_t)uio->uio_resid)
			chunk = uio->uio_resid;
		if (offset + chunk > filesize)
			chunk = filesize - offset;

		error = nbfs_read_block(nmp, physblk, block_buf);
		if (error != 0)
			return (error);

		error = uiomove(block_buf + blkoff, (int)chunk, uio);
		if (error != 0)
			return (error);
	}

	return (0);
}

static int
nbfs_vop_open(struct vop_open_args *ap)
{
	return (0);
}

static int
nbfs_vop_close(struct vop_close_args *ap)
{
	return (0);
}

/* ------------------------------------------------------------------ */
/* Vnode operations vector.                                            */
/* ------------------------------------------------------------------ */

/*
 * We use the older-style vop_vector with explicit function pointer
 * assignment.  This avoids the complexity of the epoch-based VOP
 * macros while remaining fully compatible with FreeBSD's VFS layer.
 */
struct vop_vector nbfs_vnodeops = {
	.vop_default	= &default_vnodeops,

	.vop_lookup	= (vop_lookup_t *)nbfs_vop_lookup,
	.vop_readdir	= (vop_readdir_t *)nbfs_vop_readdir,
	.vop_getattr	= (vop_getattr_t *)nbfs_vop_getattr,
	.vop_read	= (vop_read_t *)nbfs_vop_read,
	.vop_open	= (vop_open_t *)nbfs_vop_open,
	.vop_close	= (vop_close_t *)nbfs_vop_close,
};
