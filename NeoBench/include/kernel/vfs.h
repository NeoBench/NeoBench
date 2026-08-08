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
