/*
 * NeoBench File System (NBFS) -- FreeBSD kernel module.
 *
 * This header defines the internal structures shared between the NBFS
 * VFS integration, vnode operations, node cache, and block I/O layers.
 */

#ifndef _NBFS_NBFS_H_
#define _NBFS_NBFS_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/tree.h>

/* ------------------------------------------------------------------ */
/* NBFS on-disk constants (must match include/nbfs/nbfs.h)            */
/* ------------------------------------------------------------------ */

#define NBFS_MAGIC			0x5346424e
#define NBFS_VERSION_MAJOR		1
#define NBFS_VERSION_MINOR		0

#define NBFS_BLOCK_SIZE			4096
#define NBFS_MAX_NAME			255
#define NBFS_EXTENTS_PER_INODE		12

/* Fixed block layout (4K blocks). */
#define NBFS_BLKO_SUPERBLOCK		1
#define NBFS_BLKO_BLOCK_BITMAP		2
#define NBFS_BLKO_INODE_BITMAP		3
#define NBFS_BLKO_INODE_TABLE		4
#define NBFS_BLKO_INODE_TABLE_BLOCKS	64
#define NBFS_BLKO_JOURNAL_START		(NBFS_BLKO_INODE_TABLE + \
					 NBFS_BLKO_INODE_TABLE_BLOCKS)
#define NBFS_BLKO_JOURNAL_BLOCKS	256
#define NBFS_BLKO_DATA_START		(NBFS_BLKO_JOURNAL_START + \
					 NBFS_BLKO_JOURNAL_BLOCKS)

/* Mode bits. */
#define NBFS_MODE_DIR	0x4000
#define NBFS_MODE_FILE	0x8000
#define NBFS_MODE_MASK	0xF000

/* ------------------------------------------------------------------ */
/* On-disk structures (little-endian, packed).                         */
/* ------------------------------------------------------------------ */

struct nbfs_disk_superblock {
	uint32_t ds_magic;
	uint16_t ds_version_major;
	uint16_t ds_version_minor;
	uint32_t ds_block_size;
	uint32_t ds_flags;
	uint64_t ds_total_blocks;
	uint64_t ds_free_blocks;
	uint64_t ds_total_inodes;
	uint64_t ds_free_inodes;
	uint64_t ds_root_inode;
	uint64_t ds_journal_start;
	uint64_t ds_journal_blocks;
	uint64_t ds_block_bitmap_start;
	uint64_t ds_inode_bitmap_start;
	uint64_t ds_inode_table_start;
	uint64_t ds_data_start;
	char	 ds_volume_name[64];
	uint32_t ds_crc32;
	uint8_t	 ds_reserved[128];
} __attribute__((packed));

struct nbfs_disk_extent {
	uint64_t de_start_block;
	uint32_t de_block_count;
	uint32_t de_flags;
} __attribute__((packed));

struct nbfs_disk_inode {
	uint64_t di_inode_number;
	uint16_t di_mode;
	uint16_t di_links;
	uint32_t di_uid;
	uint32_t di_gid;
	uint64_t di_size;
	uint64_t di_created;
	uint64_t di_modified;
	uint64_t di_accessed;
	struct nbfs_disk_extent di_extents[NBFS_EXTENTS_PER_INODE];
	uint32_t di_crc32;
} __attribute__((packed));

struct nbfs_disk_dirent {
	uint64_t de_inode;
	uint16_t de_record_length;
	uint8_t	 de_name_length;
	uint8_t	 de_type;
	/* Variable-length name follows. */
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* In-core mount structure.                                            */
/* ------------------------------------------------------------------ */

RB_HEAD(nbfs_node_tree, nbfs_node);

struct nbfs_mount {
	struct mount		*nm_mountp;
	struct vnode		*nm_rootvp;
	struct dev_softc	*nm_dev;	/* underlying device */
	uint32_t		 nm_blocksize;
	uint64_t		 nm_totalblocks;
	uint64_t		 nm_freeblocks;
	uint64_t		 nm_totalinodes;
	uint64_t		 nm_freeinodes;
	uint64_t		 nm_rootinode;
	uint64_t		 nm_datastart;
	char			 nm_volname[64];

	/* Superblock copy (host byte order). */
	struct nbfs_disk_superblock nm_super;

	/* Node cache. */
	struct nbfs_node_tree	 nm_nodes;
	int			 nm_nnodes;
	struct rwlock		 nm_nodelock;
};

/* Extract mount private data. */
#define VFS_TO_NBFS(mp)	((struct nbfs_mount *)(mp)->mnt_data)

/* ------------------------------------------------------------------ */
/* In-core node (vnode private data).                                  */
/* ------------------------------------------------------------------ */

struct nbfs_node {
	/* Red-black tree key. */
	ino_t			 nn_ino;
	RB_ENTRY(nbfs_node)	 nn_entry;

	/* Cached on-disk inode (host byte order where applicable). */
	struct nbfs_disk_inode	 nn_disk;

	/* Back-pointer. */
	struct nbfs_mount	*nn_mp;
	struct vnode		*nn_vp;

	uint64_t		 nn_size;
	uint16_t		 nn_mode;
	uint16_t		 nn_links;
};

#define VNODE_TO_NBN(vp)	((struct nbfs_node *)(vp)->v_data)

/* ------------------------------------------------------------------ */
/* Vnode operations vector.                                            */
/* ------------------------------------------------------------------ */

extern struct vop_vector nbfs_vnodeops;

/* ------------------------------------------------------------------ */
/* Function prototypes.                                                */
/* ------------------------------------------------------------------ */

/* Mount (nbfs_mount.c) */
int nbfs_mount(struct mount *mp, struct thread *td);
int nbfs_unmount(struct mount *mp, int mntflags, struct thread *td);
int nbfs_root(struct mount *mp, int flags, struct vnode **vpp,
    struct thread *td);
int nbfs_statfs(struct mount *mp, struct statfs *sbp, struct thread *td);
int nbfs_sync(struct mount *mp, int waitfor, kauth_cred_t cred,
    struct thread *td);

/* Node cache (nbfs_node.c) */
int  nbfs_nget(struct nbfs_mount *nmp, ino_t ino, struct nbfs_node **npp);
void nbfs_nrele(struct nbfs_node *np);
int  nbfs_ninit(struct nbfs_mount *nmp);
void nbfs_ndestroy(struct nbfs_mount *nmp);

/* Block I/O (nbfs_io.c) */
int  nbfs_read_block(struct nbfs_mount *nmp, uint64_t blkno, void *buf);
int  nbfs_read_inode(struct nbfs_mount *nmp, uint64_t ino,
    struct nbfs_disk_inode *dip);
int  nbfs_read_superblock(struct nbfs_mount *nmp);

/* CRC32 (nbfs_crc32.c) */
uint32_t nbfs_crc32(const void *buf, uint32_t len);

#endif /* _NBFS_NBFS_H_ */
