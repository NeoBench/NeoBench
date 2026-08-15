#ifndef NEOBENCH_VFS_VFS_H
#define NEOBENCH_VFS_VFS_H

#include "vfs/filesystem.h"

int vfs_init(void);
void vfs_shutdown(void);

int vfs_register_filesystem(vfs_filesystem_t *fs);
int vfs_unregister_filesystem(vfs_filesystem_t *fs);
vfs_filesystem_t *vfs_find_filesystem(const char *name);

#endif
