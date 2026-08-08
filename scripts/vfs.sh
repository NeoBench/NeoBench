#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating Virtual File System"
echo "========================================"

mkdir -p kernel/fs
mkdir -p include/kernel

###############################################################################
# VFS Header
###############################################################################

cat > include/kernel/vfs.h <<'EOF'
#ifndef NB_VFS_H
#define NB_VFS_H

#include <neobench/types.h>

#define VFS_MAX_FILESYSTEMS 16
#define VFS_MAX_MOUNTS      16
#define VFS_NAME_LENGTH     32

typedef struct filesystem filesystem_t;
typedef struct vnode vnode_t;

struct vnode
{
    uint32_t inode;
    uint32_t size;
    uint32_t flags;
    void *private_data;
};

struct filesystem
{
    char name[VFS_NAME_LENGTH];

    int (*mount)(void);
    int (*unmount)(void);

    int (*open)(const char *);
    int (*close)(int);

    int (*read)(int, void *, uint32_t);
    int (*write)(int, const void *, uint32_t);

    int (*mkdir)(const char *);
};

void vfs_init(void);

int vfs_register(filesystem_t *);

int vfs_mount(const char *fs);

int vfs_open(const char *);

int vfs_close(int);

int vfs_read(int, void *, uint32_t);

int vfs_write(int, const void *, uint32_t);

#endif
EOF

###############################################################################
# VFS Core
###############################################################################

cat > kernel/fs/vfs.c <<'EOF'
#include <kernel/vfs.h>

static filesystem_t *filesystems[VFS_MAX_FILESYSTEMS];

void vfs_init(void)
{
    for(int i=0;i<VFS_MAX_FILESYSTEMS;i++)
        filesystems[i]=0;
}

int vfs_register(filesystem_t *fs)
{
    for(int i=0;i<VFS_MAX_FILESYSTEMS;i++)
    {
        if(filesystems[i]==0)
        {
            filesystems[i]=fs;
            return 0;
        }
    }

    return -1;
}

int vfs_mount(const char *fs)
{
    (void)fs;
    return 0;
}

int vfs_open(const char *path)
{
    (void)path;
    return -1;
}

int vfs_close(int fd)
{
    (void)fd;
    return 0;
}

int vfs_read(int fd, void *buffer, uint32_t size)
{
    (void)fd;
    (void)buffer;
    (void)size;
    return 0;
}

int vfs_write(int fd,const void *buffer,uint32_t size)
{
    (void)fd;
    (void)buffer;
    (void)size;
    return 0;
}
EOF

echo
echo "VFS created."
