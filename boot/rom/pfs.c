/*
 * PFS lookups -- the read side of the file store tools/mkpfs.py builds.
 *
 * Nothing here is hashed or sorted: the store is a handful of files built
 * into a ROM image, so a linear walk is both the smallest and the fastest
 * thing that can answer.
 */
#include "pfs.h"

const struct pfs_node *pfs_find(const char *path)
{
    unsigned i;

    if (!path)
        return 0;
    if (!*path || *path == '/')          /* "" or "/" is the root         */
        return &nb_pfs_nodes[PFS_ROOT];

    for (i = 0; i < nb_pfs_count; i++)
    {
        const char *a = nb_pfs_nodes[i].path;
        const char *b = path;

        if (a[0] == '\0')
            continue;
        while (*a && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return &nb_pfs_nodes[i];
    }
    return 0;
}

unsigned pfs_first_child(unsigned dir)
{
    unsigned i;

    for (i = 0; i < nb_pfs_count; i++)
        if (nb_pfs_nodes[i].parent == dir && i != dir)
            return i;
    return PFS_NONE;
}

unsigned pfs_next_child(unsigned dir, unsigned after)
{
    unsigned i;

    for (i = after + 1; i < nb_pfs_count; i++)
        if (nb_pfs_nodes[i].parent == dir)
            return i;
    return PFS_NONE;
}
