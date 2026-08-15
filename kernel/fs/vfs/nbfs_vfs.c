#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "nbfs_vfs.h"
#include "libnbfs.h"

int vfs_nbfs_lookup(
    vfs_filesystem_t *fs,
    uint64_t parent_inode,
    const char *name,
    uint64_t *result_inode,
    uint32_t *result_mode
)
{
    printf("DEBUG vfs_nbfs_lookup: parent=%llu name=\"%s\"\n",
           (unsigned long long)parent_inode,
           name ? name : "(null)");
    nbfs_context_t *ctx;
    nbfs_inode_t inode;

    if (!fs || !fs->private_data ||
        !name || !result_inode || !result_mode)
        return -1;

    ctx = (nbfs_context_t *)fs->private_data;

    if (nbfs_lookup(
            ctx,
            parent_inode,
            name,
            result_inode) != 0)
        return -1;

    if (nbfs_read_inode(
            ctx,
            *result_inode,
            &inode) != 0)
        return -1;

    *result_mode = inode.mode;

    return 0;
}
