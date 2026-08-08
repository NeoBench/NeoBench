#include "boot.h"

#include "../cache/cache.h"
#include "../fs/disk.h"
#include "../fs/nbfs.h"

int loader_main(void)
{
    cache_init();

    if (!disk_init())
        return -1;

    if (!nbfs_mount())
        return -1;

    return 0;
}
