#include <nbfs/directory.h>

int nbfs_lookup(
    const char *path,
    uint32_t *inode)
{
    (void)path;

    *inode = 0;

    /*
     * Next stage:
     *
     * /
     * └── boot
     *      └── kernel.elf
     */

    return 0;
}
