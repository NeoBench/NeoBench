#include "vfs/vfs.h"

#include "console.h"

static int vfs_ready;

int vfs_init(void)
{
    if (vfs_ready)
        return 0;

    console_write("VFS:       initializing... ");

    /*
     * Core VFS objects are currently statically managed.
     * Filesystem registration and mounting will be added next.
     */
    vfs_ready = 1;

    console_write("OK\n");

    return 0;
}

void vfs_shutdown(void)
{
    vfs_ready = 0;
}
