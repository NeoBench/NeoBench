#include <stdint.h>
#include "file.h"
#include "disk.h"

#include <nbfs/inode.h>

int nb_open(const char *path, nb_file_t *file)
{
    (void)path;
    (void)file;

    return 0;
}

int nb_read(nb_file_t *file, void *buffer, uint32_t bytes)
{
    (void)file;
    (void)buffer;
    (void)bytes;

    return 0;
}

int nb_read_inode_file(const nbfs_inode_t *inode, void *buffer)
{
    (void)inode;
    (void)buffer;

    /*
     * TODO:
     * Read all direct extents from disk.
     */

    return 1;
}

int nb_close(nb_file_t *file)
{
    (void)file;

    return 0;
}
