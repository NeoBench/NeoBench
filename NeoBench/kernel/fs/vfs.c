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
