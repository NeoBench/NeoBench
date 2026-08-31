/*
 * NeoBench File System (NBFS) -- VFS operations.
 *
 * Mount, unmount, root, statfs, and sync.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/module.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/namei.h>
#include <sys/fcntl.h>

#include "nbfs.h"

MALLOC_DEFINE(M_NBFSMNT, "nbfs mount", "NBFS mount structure");

/* ------------------------------------------------------------------ */
/* VFS operations.                                                     */
/* ------------------------------------------------------------------ */

static int
nbfs_vfs_mount(struct mount *mp)
{
	struct thread *td = curthread;
	struct nbfs_mount *nmp;
	struct nbfs_node *np;
	char *from;
	int error;

	/*拒绝 mount update. */
	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	/* The device path is passed via the "from" option. */
	error = vfs_getopt(mp->mnt_optnew, "from", (void **)&from, NULL);
	if (error)
		return (EINVAL);

	/* Allocate the mount structure. */
	nmp = malloc(sizeof(*nmp), M_NBFSMNT, M_WAITOK | M_ZERO);
	nmp->nm_mountp = mp;

	/* Initialise the node cache. */
	error = nbfs_ninit(nmp);
	if (error != 0) {
		free(nmp, M_NBFSMNT);
		return (error);
	}

	/*
	 * Read the superblock.  The underlying device vnode is
	 * mp->mnt_vnodecovered; bread() uses it for buffer I/O.
	 * Store it temporarily so nbfs_read_block() can find it.
	 */
	nmp->nm_rootvp = NULL;  /* not yet */
	error = nbfs_read_superblock(nmp);
	if (error != 0) {
		printf("nbfs: cannot read superblock from %s\n", from);
		nbfs_ndestroy(nmp);
		free(nmp, M_NBFSMNT);
		return (error);
	}

	printf("nbfs: mounting %s -- volume \"%s\", %llu blocks, "
	    "%llu inodes, root inode %llu\n",
	    from,
	    nmp->nm_volname,
	    (unsigned long long)nmp->nm_totalblocks,
	    (unsigned long long)nmp->nm_totalinodes,
	    (unsigned long long)nmp->nm_rootinode);

	/*
	 * Create the root vnode.  We need the device vnode for bread(),
	 * so stash the covered vnode in the mount structure before
	 * calling nbfs_nget() which will call nbfs_read_inode() ->
	 * nbfs_read_block() -> bread(vp, ...).
	 */
	nmp->nm_rootvp = mp->mnt_vnodecovered;
	error = nbfs_nget(nmp, (ino_t)nmp->nm_rootinode, &np);
	if (error != 0) {
		printf("nbfs: cannot create root node\n");
		nbfs_ndestroy(nmp);
		free(nmp, M_NBFSMNT);
		return (error);
	}

	/* The root vnode is now cached; clear the temporary pointer. */
	nmp->nm_rootvp = NULL;
	np->nn_vp->v_vnflag = VV_ROOT;
	nmp->nm_rootvp = np->nn_vp;
	nbfs_nrele(np);

	/* Finish mount. */
	mp->mnt_data = nmp;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTKLookupShared;
	mp->mnt_maxsymlinklen = 0;
	vfs_getnewfsid(mp);

	strlcpy(mp->mnt_stat.f_fstypename, "nbfs", MFSNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, from, MNAMELEN);
	mp->mnt_stat.f_iosize = NBFS_BLOCK_SIZE;
	mp->mnt_stat.f_blocks = nmp->nm_totalblocks;
	mp->mnt_stat.f_bfree = nmp->nm_freeblocks;
	mp->mnt_stat.f_bavail = nmp->nm_freeblocks;
	mp->mnt_stat.f_files = nmp->nm_totalinodes;
	mp->mnt_stat.f_ffree = nmp->nm_freeinodes;
	mp->mnt_stat.f_namemax = NBFS_MAX_NAME;

	return (0);
}

static int
nbfs_vfs_unmount(struct mount *mp, int mntflags, struct thread *td)
{
	struct nbfs_mount *nmp = VFS_TO_NBFS(mp);
	int flags;

	flags = (mntflags & MNT_FORCE) ? FORCECLOSE : 0;

	/* Flush and reclaim all vnodes. */
	int error = vflush(mp, 1, flags, td);
	if (error != 0)
		return (error);

	nbfs_ndestroy(nmp);
	free(nmp, M_NBFSMNT);
	mp->mnt_data = NULL;
	return (0);
}

static int
nbfs_vfs_root(struct mount *mp, int flags, struct vnode **vpp,
    struct thread *td)
{
	struct nbfs_mount *nmp = VFS_TO_NBFS(mp);
	int error;

	KASSERT(nmp->nm_rootvp != NULL, ("nbfs: NULL root vnode"));

	error = vget(nmp->nm_rootvp, flags | LK_RETRY);
	if (error == 0)
		*vpp = nmp->nm_rootvp;

	return (error);
}

static int
nbfs_vfs_statfs(struct mount *mp, struct statfs *sbp, struct thread *td)
{
	struct nbfs_mount *nmp = VFS_TO_NBFS(mp);

	sbp->f_bsize = NBFS_BLOCK_SIZE;
	sbp->f_iosize = NBFS_BLOCK_SIZE;
	sbp->f_blocks = nmp->nm_totalblocks;
	sbp->f_bfree = nmp->nm_freeblocks;
	sbp->f_bavail = nmp->nm_freeblocks;
	sbp->f_files = nmp->nm_totalinodes;
	sbp->f_ffree = nmp->nm_freeinodes;
	sbp->f_namemax = NBFS_MAX_NAME;

	return (0);
}

static int
nbfs_vfs_sync(struct mount *mp, int waitfor, kauth_cred_t cred,
    struct thread *td)
{
	/* NBFS v1 is read-only; nothing to flush. */
	return (0);
}

/* ------------------------------------------------------------------ */
/* VFS operations vector.                                              */
/* ------------------------------------------------------------------ */

static struct vfsops nbfs_vfsops = {
	.vfs_mount	= nbfs_vfs_mount,
	.vfs_unmount	= nbfs_vfs_unmount,
	.vfs_root	= nbfs_vfs_root,
	.vfs_statfs	= nbfs_vfs_statfs,
	.vfs_sync	= nbfs_vfs_sync,
};

VFS_SET(nbfs_vfsops, nbfs, VFCF_READONLY);
