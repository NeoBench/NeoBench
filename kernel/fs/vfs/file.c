#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "vfs/file.h"
#include "vfs/vnode.h"
#include "vfs/filesystem.h"
#include "../../include/nbfs.h"

int vfs_file_init(
    vfs_file_t *file,
    vfs_vnode_t *vnode,
    uint32_t flags)
{
    if (!file || !vnode)
        return -1;

    /*
     * Keep our own vnode copy.  vfs_open() may hand us a
     * temporary vnode produced by vfs_lookup(), so retaining
     * the caller's pointer is unsafe.
     */
    file->vnode_storage = *vnode;
    file->vnode = &file->vnode_storage;
    file->offset = 0;
    file->flags = flags;

    return 0;
}
void vfs_file_destroy(vfs_file_t *file)
{
    if (!file)
        return;

    if (file->vnode)
    {
        vfs_vnode_put(file->vnode);
        file->vnode = NULL;
    }

    file->offset = 0;
    file->flags = 0;
}

ssize_t vfs_file_read(
    vfs_file_t *file,
    void *buffer,
    size_t size)
{
    nbfs_inode_t inode;
    uint64_t remaining;
    uint64_t requested;
    uint64_t file_size;
    int rc;

    if (!file || !file->vnode)
        return -1;

    if (size > 0 && !buffer)
        return -1;

    if (size == 0)
        return 0;

    if (file->vnode->type != VFS_VNODE_REG)
        return -1;

    if (!file->vnode->fs)
        return -1;

    rc = nbfs_kernel_read_inode(
        file->vnode->fs,
        file->vnode->ino,
        &inode);

    if (rc != 0)
        return -1;

    file_size = inode.size;

    if (file->offset >= file_size)
        return 0;

    remaining = file_size - file->offset;
    requested = (uint64_t)size;

    if (requested > remaining)
        requested = remaining;

    rc = (int)nbfs_kernel_read(
        file->vnode->fs,
        file->vnode->ino,
        file->offset,
        buffer,
        requested);

    if (rc < 0 || (uint64_t)rc != requested)
        return -1;

    file->offset += requested;

    return (ssize_t)requested;
}

int vfs_open(
    const vfs_path_t *root,
    const char *path,
    uint32_t flags,
    vfs_file_t *file)
{
    vfs_path_t current;
    vfs_vnode_t next;

    const char *p;
    const char *start;
    size_t len;

    char component[256];

    if (!root || !root->vnode || !path || !file)
        return -1;

    if (path[0] != '/')
        return -1;

    if (vfs_path_init(&current, root->vnode) != 0)
        return -1;

    p = path;

    while (*p == '/')
        p++;

    /*
     * Opening "/" returns the supplied root vnode.
     */
    if (*p == '\0')
    {
        int rc = vfs_file_init(
            file,
            current.vnode,
            flags);

        vfs_path_destroy(&current);

        return rc;
    }

    /*
     * Resolve:
     *
     *     /docs/readme.txt
     *
     * as:
     *
     *     root -> docs -> readme.txt
     */
    while (*p != '\0')
    {
        start = p;

        while (*p != '\0' && *p != '/')
            p++;

        len = (size_t)(p - start);

        if (len == 0)
        {
            while (*p == '/')
                p++;

            continue;
        }

        if (len >= sizeof(component))
        {
            vfs_path_destroy(&current);
            return -1;
        }

        memcpy(component, start, len);
        component[len] = '\0';

        if (vfs_lookup(
                &current,
                component,
                &next) != 0)
        {
            vfs_path_destroy(&current);
            return -1;
        }

        vfs_path_destroy(&current);

        if (vfs_path_init(&current, &next) != 0)
        {
            vfs_vnode_put(&next);
            return -1;
        }

        /*
         * vfs_lookup() returns an owned vnode reference.
         * vfs_path_init() acquired another reference.
         */
        vfs_vnode_put(&next);

        while (*p == '/')
            p++;
    }

    if (vfs_file_init(
            file,
            current.vnode,
            flags) != 0)
    {
        vfs_path_destroy(&current);
        return -1;
    }

    vfs_path_destroy(&current);

    return 0;
}

int64_t vfs_file_seek(
    vfs_file_t *file,
    int64_t offset,
    int whence)
{
    nbfs_inode_t inode;
    int64_t base;
    int64_t new_offset;

    if (!file || !file->vnode)
        return -1;

    switch (whence)
    {
        case VFS_SEEK_SET:
            base = 0;
            break;

        case VFS_SEEK_CUR:
            if (file->offset > INT64_MAX)
                return -1;

            base = (int64_t)file->offset;
            break;

        case VFS_SEEK_END:
            if (!file->vnode->fs ||
                !file->vnode->fs->private_data)
                return -1;

            if (nbfs_kernel_read_inode(
                    file->vnode->fs,
                    file->vnode->ino,
                    &inode) != 0)
                return -1;

            if (inode.size > INT64_MAX)
                return -1;

            base = (int64_t)inode.size;
            break;

        default:
            return -1;
    }

    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset))
        return -1;

    new_offset = base + offset;

    if (new_offset < 0)
        return -1;

    file->offset = (uint64_t)new_offset;

    return new_offset;
}

int64_t vfs_file_tell(const vfs_file_t *file)
{
    if (!file || !file->vnode)
        return -1;

    if (file->offset > INT64_MAX)
        return -1;

    return (int64_t)file->offset;
}
