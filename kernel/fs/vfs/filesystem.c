#include <stdint.h>

#include "vfs/filesystem.h"

int vfs_filesystem_init(
    vfs_filesystem_t *fs,
    const char *name,
    uint32_t block_size,
    uint64_t flags
)
{
    if (!fs || !name || block_size == 0)
        return -1;

    fs->name = name;
    fs->block_size = block_size;
    fs->flags = flags;
    fs->private_data = 0;
    fs->lookup = 0;
    fs->get_size = 0;
    fs->read_file = 0;

    return 0;
}

void vfs_filesystem_destroy(vfs_filesystem_t *fs)
{
    if (!fs)
        return;

    fs->name = 0;
    fs->block_size = 0;
    fs->flags = 0;
    fs->private_data = 0;
    fs->lookup = 0;
    fs->get_size = 0;
    fs->read_file = 0;
}
