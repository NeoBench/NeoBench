/*
 * path.c
 * NeoBench libNBFS
 *
 * NBFS pathname resolution.
 *
 * Resolves absolute and relative paths by walking directory
 * entries through the existing nbfs_lookup() API.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"

#define NBFS_ROOT_INODE 1

/*
 * Return the parent directory inode for an inode.
 *
 * NBFS directories contain:
 *
 *     "."  -> self
 *     ".." -> parent
 *
 * For the root directory, ".." resolves to root itself.
 */
static int path_parent(
    nbfs_context_t *ctx,
    uint64_t inode,
    uint64_t *parent)
{
    if (!ctx || !parent)
        return -1;

    if (inode == NBFS_ROOT_INODE)
    {
        *parent = NBFS_ROOT_INODE;
        return 0;
    }

    return nbfs_lookup(
        ctx,
        inode,
        "..",
        parent);
}

/*
 * Resolve a single pathname component.
 */
static int path_lookup_component(
    nbfs_context_t *ctx,
    uint64_t current,
    const char *component,
    uint64_t *next)
{
    if (!ctx || !component || !next)
        return -1;

    if (component[0] == '\0')
    {
        *next = current;
        return 0;
    }

    /*
     * "." means current directory.
     */
    if (strcmp(component, ".") == 0)
    {
        *next = current;
        return 0;
    }

    /*
     * ".." means parent directory.
     */
    if (strcmp(component, "..") == 0)
    {
        return path_parent(
            ctx,
            current,
            next);
    }

    /*
     * Normal directory entry.
     */
    return nbfs_lookup(
        ctx,
        current,
        component,
        next);
}

/*
 * Resolve an NBFS pathname.
 *
 * Absolute:
 *
 *     /
 *     /docs
 *     /docs/readme.txt
 *     /docs/subdir/file.txt
 *
 * Relative:
 *
 *     .
 *     ..
 *     docs
 *     docs/readme.txt
 *
 * Repeated '/' characters are ignored.
 */
int nbfs_resolve_path(
    nbfs_context_t *ctx,
    uint64_t start_inode,
    const char *path,
    uint64_t *result_inode)
{
    uint64_t current;
    const char *p;

    if (!ctx || !path || !result_inode)
        return -1;

    if (path[0] == '\0')
        return -1;

    /*
     * Absolute path starts at root.
     */
    if (path[0] == '/')
    {
        current = NBFS_ROOT_INODE;
        p = path;

        /*
         * Skip all leading slashes.
         */
        while (*p == '/')
            p++;

        /*
         * "/" means root.
         */
        if (*p == '\0')
        {
            *result_inode = NBFS_ROOT_INODE;
            return 0;
        }
    }
    else
    {
        /*
         * Relative path starts at start_inode.
         */
        if (start_inode == 0)
            return -1;

        current = start_inode;
        p = path;
    }

    while (*p != '\0')
    {
        char component[256];
        size_t length = 0;
        uint64_t next = 0;

        /*
         * Skip repeated '/' characters.
         */
        while (*p == '/')
            p++;

        if (*p == '\0')
            break;

        /*
         * Extract one component.
         */
        while (*p != '\0' && *p != '/')
        {
            if (length >= sizeof(component) - 1)
                return -1;

            component[length++] = *p++;
        }

        component[length] = '\0';

        if (length == 0)
            continue;

        /*
         * Resolve this component.
         */
        if (path_lookup_component(
                ctx,
                current,
                component,
                &next) != 0)
        {
            return -1;
        }

        /*
         * next is always assigned by path_lookup_component()
         * on success.
         */
        current = next;
    }

    *result_inode = current;

    return 0;
}
