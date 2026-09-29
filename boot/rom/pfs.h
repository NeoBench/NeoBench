#ifndef NB_PFS_H
#define NB_PFS_H

/*
 * PFS -- the NeoBench preference file store.
 *
 * A read-only directory tree compiled into the ROM by tools/mkpfs.py from
 * the system/ tree in the source.  NeoBench mounts nothing and reads no
 * block device at boot, so the Prefs directory the kernel loads its
 * configuration from is this: a flat table of nodes, each naming its
 * parent, with file bodies hanging off const byte arrays in ROM.
 *
 * A body always carries one extra NUL past `size`, so text files double
 * as C strings; anything binary is read through pfs_text() sized by
 * `size` and never assumes termination.
 */

#define PFS_NONE  0xFFFFFFFFu
#define PFS_ROOT  0u

struct pfs_node
{
    const char          *name;   /* basename; "/" for the root          */
    const char          *path;   /* "" for root, else "Config/x.cfg"     */
    unsigned             parent; /* index of the containing directory   */
    const unsigned char *data;   /* file body (0 for a directory)       */
    unsigned             size;   /* body length, the NUL not counted    */
    unsigned             dir;    /* non-zero for a directory            */
};

extern const struct pfs_node nb_pfs_nodes[];
extern const unsigned        nb_pfs_count;

/* Node for an absolute path without a leading slash, 0 if unknown. */
const struct pfs_node *pfs_find(const char *path);

/* Enumerate the children of a directory: first index, then the one
 * after a given index, PFS_NONE at either end. */
unsigned pfs_first_child(unsigned dir);
unsigned pfs_next_child(unsigned dir, unsigned after);

#endif /* NB_PFS_H */
