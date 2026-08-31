/*
 * NeoBench File System (NBFS) -- Node (inode) cache.
 *
 * Maps NBFS inode numbers to vnodes.  Uses a red-black tree keyed on
 * inode number for O(log n) lookup.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/tree.h>

#include "nbfs.h"

MALLOC_DEFINE(M_NBFSNODE, "nbfs node", "NBFS in-core node");

/* Red-black tree comparison. */
static int
nbfs_node_cmp(struct nbfs_node *a, struct nbfs_node *b)
{
	if (a->nn_ino < b->nn_ino)
		return (-1);
	if (a->nn_ino > b->nn_ino)
		return (1);
	return (0);
}

RB_GENERATE(nbfs_node_tree, nbfs_node, nn_entry, nbfs_node_cmp);

/*
 * Initialise the node cache for a mount.
 */
int
nbfs_ninit(struct nbfs_mount *nmp)
{
	RB_INIT(&nmp->nm_nodes);
	nmp->nm_nnodes = 0;
	rw_init(&nmp->nm_nodelock, "nbfs node");
	return (0);
}

/*
 * Destroy all cached nodes on unmount.
 */
void
nbfs_ndestroy(struct nbfs_mount *nmp)
{
	struct nbfs_node *np;

	rw_wlock(&nmp->nm_nodelock);
	while ((np = RB_ROOT(&nmp->nm_nodes)) != NULL) {
		RB_REMOVE(nbfs_node_tree, &nmp->nm_nodes, np);
		nmp->nm_nnodes--;

		if (np->nn_vp != NULL) {
			np->nn_vp->v_data = NULL;
			vrecycle(np->nn_vp);
		}
		free(np, M_NBFSNODE);
	}
	rw_wunlock(&nmp->nm_nodelock);

	rw_destroy(&nmp->nm_nodelock);
}

/*
 * Look up or create an in-core node for the given inode number.
 * If the node is new its on-disk inode is read from the volume.
 * Returns with the node locked (via the vnode).
 */
int
nbfs_nget(struct nbfs_mount *nmp, ino_t ino, struct nbfs_node **npp)
{
	struct nbfs_node key, *np;
	struct nbfs_disk_inode dip;
	struct vnode *vp;
	int error;

	*npp = NULL;

	/* Check the cache first (shared lock). */
	key.nn_ino = ino;
	rw_rlock(&nmp->nm_nodelock);
	np = RB_FIND(nbfs_node_tree, &nmp->nm_nodes, &key);
	rw_runlock(&nmp->nm_nodelock);

	if (np != NULL) {
		/* Already cached -- return it. */
		vp = np->nn_vp;
		if (vp != NULL) {
			error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error != 0)
				return (error);
		}
		*npp = np;
		return (0);
	}

	/* Read the on-disk inode. */
	error = nbfs_read_inode(nmp, ino, &dip);
	if (error != 0)
		return (error);

	/* Allocate a new vnode + node. */
	error = getnewvnode(MOUNT_NBFS, nmp->nm_mountp, &nbfs_vnodeops,
	    &vp);
	if (error != 0)
		return (error);

	np = malloc(sizeof(*np), M_NBFSNODE, M_WAITOK | M_ZERO);
	np->nn_ino = ino;
	np->nn_mp = nmp;
	np->nn_vp = vp;
	np->nn_disk = dip;
	np->nn_size = dip.di_size;
	np->nn_mode = dip.di_mode;
	np->nn_links = dip.di_links;

	vp->v_data = np;
	vp->v_vnflag = 0;
	vp->v_bufobj.bo_bsize = NBFS_BLOCK_SIZE;

	/* Insert into cache. */
	rw_wlock(&nmp->nm_nodelock);
	if (RB_FIND(nbfs_node_tree, &nmp->nm_nodes, &np) != NULL) {
		/* Lost the race -- another thread inserted first. */
		rw_wunlock(&nmp->nm_nodelock);
		vp->v_data = NULL;
		vput(vp);
		free(np, M_NBFSNODE);

		/* Re-fetch from cache. */
		key.nn_ino = ino;
		rw_rlock(&nmp->nm_nodelock);
		np = RB_FIND(nbfs_node_tree, &nmp->nm_nodes, &key);
		rw_runlock(&nmp->nm_nodelock);
		if (np == NULL)
			return (ENOENT);

		vp = np->nn_vp;
		if (vp != NULL) {
			error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error != 0)
				return (error);
		}
		*npp = np;
		return (0);
	}

	RB_INSERT(nbfs_node_tree, &nmp->nm_nodes, np);
	nmp->nm_nnodes++;
	rw_wunlock(&nmp->nm_nodelock);

	*npp = np;
	return (0);
}

/*
 * Release a reference to a node.
 */
void
nbfs_nrele(struct nbfs_node *np)
{
	if (np == NULL)
		return;

	if (np->nn_vp != NULL)
		vput(np->nn_vp);
}
