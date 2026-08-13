#ifndef VFS_VFS_H
#define VFS_VFS_H

#include "filesystem.h"
#include "mount.h"
#include "vnode.h"
#include "dentry.h"
#include "path.h"

int vfs_init(void);
void vfs_shutdown(void);

#endif
