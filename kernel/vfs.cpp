/*
 * NeoBench Bare-Metal Amiga Kernel
 * Virtual File System (VFS) Layer
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace filesystem {

static constexpr uint32 MAX_FD = 32;

struct FdSlot {
    bool      in_use;
    FileHandle fh;
};

static FdSlot s_fds[MAX_FD];

void init_vfs()
{
    for (uint32 i = 0; i < MAX_FD; i++)
        s_fds[i].in_use = false;
}

int32 open(FileHandle& fh, const char* path, uint32 mode)
{
    if (!path) return -1;
    fh.inode    = 0;
    fh.position = 0;
    fh.size     = 0;
    fh.flags    = mode;
    /* Stub: actual filesystem mount point lookup would go here. */
    return -1;  /* -1 = not found; 0 = success */
}

int32 read(FileHandle& /*fh*/, void* /*buf*/, uint32 /*size*/) { return -1; }
int32 write(FileHandle& /*fh*/, const void* /*buf*/, uint32 /*size*/) { return -1; }
void  close(FileHandle& fh) { fh.inode = 0; }

int32 readdir(const char* /*path*/, DirEntry* /*entries*/, uint32 /*max*/) { return 0; }

uint32 list_mounts(MountInfo* mounts, uint32 max)
{
    if (!mounts || max == 0) return 0;
    const char* dev = "df0";
    const char* mp  = "/";
    const char* fs  = "NBFS";
    uint32 i = 0;
    while (i < 63 && dev[i]) { mounts[0].device[i] = dev[i]; i++; }
    mounts[0].device[i] = '\0';
    i = 0;
    while (i < 63 && mp[i]) { mounts[0].mount_point[i] = mp[i]; i++; }
    mounts[0].mount_point[i] = '\0';
    i = 0;
    while (i < 15 && fs[i]) { mounts[0].fs_type[i] = fs[i]; i++; }
    mounts[0].fs_type[i] = '\0';
    mounts[0].total_blocks = 1760;
    mounts[0].free_blocks  = 0;
    mounts[0].block_size   = 512;
    return 1;
}

int32 open_fd(const char* path, uint32 flags)
{
    for (uint32 i = 3; i < MAX_FD; i++) {
        if (!s_fds[i].in_use) {
            s_fds[i].in_use = true;
            open(s_fds[i].fh, path, flags);
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32 close_fd(int32 fd)
{
    if (fd < 0 || fd >= static_cast<int32>(MAX_FD)) return -1;
    if (!s_fds[fd].in_use) return -1;
    close(s_fds[fd].fh);
    s_fds[fd].in_use = false;
    return 0;
}

int32 read_fd(int32 fd, void* buf, uint32 count)
{
    if (fd < 0 || fd >= static_cast<int32>(MAX_FD)) return -1;
    if (!s_fds[fd].in_use) return -1;
    return read(&s_fds[fd].fh, buf, count);
}

}  /* namespace filesystem */
}  /* namespace neo */
