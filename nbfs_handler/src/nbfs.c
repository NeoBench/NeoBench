#include "dospackets.h"
#include <string.h>

#define MODE_TYPE(m)  ((m) & 0xF000)
#define MODE_DIR      NBFS_MODE_DIRECTORY
#define MODE_FILE     NBFS_MODE_FILE

static void reply_err(struct nbh_rt *rt, struct nbfh_pkt *pkt, uint32_t err)
{
    pkt->res1 = 0;
    pkt->res2 = err;
    rt->reply(rt->ud, pkt);
}

static int ent_dir(struct nbh_vol *v, uint32_t inode)
{
    struct nbh_inode e;
    if (inode == v->root_inode)
        return 1;
    if (nbh_read_inode(v, inode, &e) != 0)
        return 0;
    return MODE_TYPE(e.mode) == MODE_DIR;
}

static void fib_fill_common(struct nbfh_fib *fib, uint32_t size)
{
    memset(fib, 0, NBFH_FIB_SIZE);
    fib->diskkey = 1;
    fib->protection = FIBF_WRITE | FIBF_DELETE;
    fib->fsize = size;
    fib->numblocks = (size + NBH_BLOCK_SIZE - 1) / NBH_BLOCK_SIZE;
}

static void fib_bstr(uint8_t *len_byte, char *dst, uint32_t cap,
                     const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1)
        n = cap - 1;
    *len_byte = (uint8_t)n;
    memcpy(dst, src, n);
}

static int fib_fill(struct nbh_vol *v, uint32_t inode, struct nbfh_fib *fib)
{
    struct nbh_inode e;

    if (inode == v->root_inode)
    {
        fib_fill_common(fib, 0);
        fib->dirdtype = ST_USERDIR;
        fib->entrytype = ST_USERDIR;
        fib_bstr(&fib->filename_len, fib->filename,
                 sizeof(fib->filename), v->volname);
        return 0;
    }

    if (nbh_read_inode(v, inode, &e) != 0)
        return -1;

    fib_fill_common(fib, (uint32_t)e.size);
    if (MODE_TYPE(e.mode) == MODE_DIR)
        fib->dirdtype = ST_USERDIR;
    else
        fib->dirdtype = 0;
    fib->entrytype = fib->dirdtype == ST_USERDIR ? ST_USERDIR : ST_FILE;
    return 0;
}

static int walk(struct nbh_vol *v, uint32_t dir, const char *path,
                struct nbh_ent *out)
{
    const char *p = path;
    uint32_t cur = dir;

    for (;;)
    {
        char comp[108];
        unsigned int i = 0;
        struct nbh_ent e;

        while (*p != 0 && *p != '/')
        {
            if (i < sizeof(comp) - 1)
                comp[i++] = *p;
            p++;
        }
        comp[i] = 0;

        if (i == 0 || strcmp(comp, ".") == 0)
        {
            if (*p == 0)
            {
                out->inode = cur;
                out->type = NBH_DIRENTRY_DIR;
                return 1;
            }
            p++;
            continue;
        }

        if (strcmp(comp, "..") == 0)
            return 0;

        if (!nbh_dirfind(v, cur, comp, &e))
            return 0;

        if (*p == 0)
        {
            *out = e;
            return 1;
        }
        if (e.type != NBH_DIRENTRY_DIR)
            return 0;
        cur = e.inode;
        p++;
    }
}

void nbh_serve(struct nbh_vol *vol, struct nbh_rt *rt, struct nbfh_pkt *pkt)
{
    nbfh_res1_t res1 = 0;
    nbfh_res2_t res2 = 0;

    switch (pkt->type)
    {
        case ACTION_LOCATE_OBJECT:
        {
            const char *name = (const char *)NBH_BDEC(pkt->arg[0]);
            struct nbh_ent e;
            struct nbfh_lock *lock;

            if (!name || !walk(vol, vol->root_inode, name, &e))
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            lock = (struct nbfh_lock *)rt->alloc(rt->ud, NBFH_LOCK_SIZE);
            if (!lock)
            {
                reply_err(rt, pkt, ERROR_NO_FREE_STORE);
                return;
            }
            memset(lock, 0, NBFH_LOCK_SIZE);
            lock->key = (int32_t)e.inode;
            lock->access = ACCESS_READ;
            lock->task = rt->handler_port;
            lock->volume = 0;
            res1 = (nbfh_res1_t)NBH_BENC(lock);
            break;
        }

        case ACTION_FREE_LOCK:
        {
            struct nbfh_lock *lock = (struct nbfh_lock *)NBH_BDEC(pkt->arg[0]);
            if (!lock)
            {
                reply_err(rt, pkt, ERROR_INVALID_LOCK);
                return;
            }
            rt->free(rt->ud, lock, NBFH_LOCK_SIZE);
            res1 = DOSTRUE;
            break;
        }

        case ACTION_SAME_LOCK:
        {
            struct nbfh_lock *lock = (struct nbfh_lock *)NBH_BDEC(pkt->arg[0]);
            struct nbfh_lock *nl;
            if (!lock)
            {
                reply_err(rt, pkt, ERROR_INVALID_LOCK);
                return;
            }
            nl = (struct nbfh_lock *)rt->alloc(rt->ud, NBFH_LOCK_SIZE);
            if (!nl)
            {
                reply_err(rt, pkt, ERROR_NO_FREE_STORE);
                return;
            }
            *nl = *lock;
            res1 = (nbfh_res1_t)NBH_BENC(nl);
            break;
        }

        case ACTION_EXAMINE_OBJECT:
        {
            struct nbfh_lock *lock = (struct nbfh_lock *)NBH_BDEC(pkt->arg[0]);
            struct nbfh_fib *fib = (struct nbfh_fib *)NBH_BDEC(pkt->arg[1]);
            if (!lock || !fib || lock->key <= 0)
            {
                reply_err(rt, pkt, ERROR_OBJECT_WRONG_TYPE);
                return;
            }
            if (fib_fill(vol, (uint32_t)lock->key, fib) != 0)
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            res1 = DOSTRUE;
            break;
        }

        case ACTION_EXAMINE_NEXT:
        {
            struct nbfh_lock *lock = (struct nbfh_lock *)NBH_BDEC(pkt->arg[0]);
            struct nbfh_fib *fib = (struct nbfh_fib *)NBH_BDEC(pkt->arg[1]);
            uint32_t ordinal;
            struct nbh_ent e;
            char name[NBH_DIRENTRY_MAXNAME];
            struct nbh_inode ie;

            if (!lock || !fib || !ent_dir(vol, (uint32_t)lock->key))
            {
                reply_err(rt, pkt, ERROR_OBJECT_WRONG_TYPE);
                return;
            }
            ordinal = fib->diskkey == 0 ? 1 : fib->diskkey;
            if (!nbh_direnum(vol, (uint32_t)lock->key, ordinal - 1, &e,
                             name, sizeof(name)))
            {
                fib_fill_common(fib, 0);
                fib->diskkey = ordinal + 1;
                reply_err(rt, pkt, ERROR_NO_MORE_ENTRIES);
                return;
            }
            fib_fill_common(fib, 0);
            fib->diskkey = ordinal + 1;
            if (nbh_read_inode(vol, e.inode, &ie) == 0)
                fib->fsize = (uint32_t)ie.size;
            fib->numblocks = (fib->fsize + NBH_BLOCK_SIZE - 1) / NBH_BLOCK_SIZE;
            if (e.type == NBH_DIRENTRY_DIR)
                fib->dirdtype = ST_USERDIR;
            else
                fib->dirdtype = ST_FILE;
            fib->entrytype = fib->dirdtype;
            fib_bstr(&fib->filename_len, fib->filename,
                     sizeof(fib->filename), name);
            res1 = DOSTRUE;
            break;
        }

        case ACTION_DISK_INFO:
        {
            struct nbfh_info *info = (struct nbfh_info *)NBH_BDEC(pkt->arg[0]);
            uint32_t num_used;
            if (!info)
            {
                reply_err(rt, pkt, ERROR_DEVICE_NOT_MOUNTED);
                return;
            }
            memset(info, 0, NBFH_INFO_SIZE);
            info->disk_state = ID_WRITE_PROTECTED;
            nbh_volstat(vol, &info->num_blocks, &num_used,
                        (uint32_t *)&info->bytes_per_block);
            info->num_blocks_used = num_used;
            info->disk_type = NBFS_DISKTYPE;
            res1 = DOSTRUE;
            break;
        }

        case ACTION_INFO:
        {
            struct nbfh_info *info = (struct nbfh_info *)NBH_BDEC(pkt->arg[1]);
            uint32_t num_used;
            if (!info)
            {
                reply_err(rt, pkt, ERROR_DEVICE_NOT_MOUNTED);
                return;
            }
            memset(info, 0, NBFH_INFO_SIZE);
            info->disk_state = ID_WRITE_PROTECTED;
            nbh_volstat(vol, &info->num_blocks, &num_used,
                        (uint32_t *)&info->bytes_per_block);
            info->num_blocks_used = num_used;
            info->disk_type = NBFS_DISKTYPE;
            res1 = DOSTRUE;
            break;
        }

        case ACTION_FINDINPUT:
        case ACTION_FINDUPDATE:
        case ACTION_FH_FROM_LOCK:
        {
            struct nbfh_lock *lock = (struct nbfh_lock *)NBH_BDEC(pkt->arg[0]);
            struct nbfh_handle *fh;
            struct nbh_inode e;
            if (!lock || nbh_read_inode(vol, (uint32_t)lock->key, &e) != 0)
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            if (MODE_TYPE(e.mode) != MODE_FILE)
            {
                reply_err(rt, pkt, ERROR_OBJECT_WRONG_TYPE);
                return;
            }
            fh = (struct nbfh_handle *)rt->alloc(rt->ud, NBFH_HANDLE_SIZE);
            if (!fh)
            {
                reply_err(rt, pkt, ERROR_NO_FREE_STORE);
                return;
            }
            memset(fh, 0, NBFH_HANDLE_SIZE);
            fh->type = rt->handler_port;
            fh->pos = 0;
            fh->end = (int32_t)e.size;
            fh->arg1 = (uint32_t)lock->key;
            res1 = (nbfh_res1_t)NBH_BENC(fh);
            break;
        }

        case ACTION_FINDOUTPUT:
            reply_err(rt, pkt, ERROR_WRITE_PROTECTED);
            return;

        case ACTION_READ:
        {
            struct nbfh_handle *fh =
                (struct nbfh_handle *)NBH_BDEC(pkt->arg[0]);
            uint8_t *buf = (uint8_t *)NBH_PDEC(pkt->arg[1]);
            int32_t want = (int32_t)pkt->arg[2];
            struct nbh_inode e;
            int got;

            if (!fh || !buf || want < 0)
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            if (nbh_read_inode(vol, fh->arg1, &e) != 0)
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            got = nbh_read(vol, &e, (uint64_t)fh->pos, buf, (uint32_t)want);
            if (got < 0)
            {
                reply_err(rt, pkt, ERROR_SEEK_ERROR);
                return;
            }
            fh->pos += got;
            res1 = got;
            break;
        }

        case ACTION_SEEK:
        {
            struct nbfh_handle *fh =
                (struct nbfh_handle *)NBH_BDEC(pkt->arg[0]);
            int32_t offset = (int32_t)pkt->arg[1];
            int32_t mode = (int32_t)pkt->arg[2];
            int32_t old;
            int32_t newpos = 0;

            if (!fh)
            {
                reply_err(rt, pkt, ERROR_OBJECT_NOT_FOUND);
                return;
            }
            old = fh->pos;
            switch (mode)
            {
                case OFFSET_BEGINNING:
                    newpos = offset;
                    break;
                case OFFSET_CURRENT:
                    newpos = old + offset;
                    break;
                case OFFSET_END:
                    newpos = fh->end + offset;
                    break;
                default:
                    reply_err(rt, pkt, ERROR_SEEK_ERROR);
                    return;
            }
            if (newpos < 0)
            {
                reply_err(rt, pkt, ERROR_SEEK_ERROR);
                return;
            }
            fh->pos = newpos;
            res1 = DOSTRUE;
            res2 = (uint32_t)old;
            break;
        }

        case ACTION_WRITE:
        {
            struct nbfh_handle *fh =
                (struct nbfh_handle *)NBH_BDEC(pkt->arg[0]);
            if (fh)
                rt->free(rt->ud, fh, NBFH_HANDLE_SIZE);
            reply_err(rt, pkt, ERROR_WRITE_PROTECTED);
            return;
        }

        case ACTION_END:
        {
            struct nbfh_handle *fh =
                (struct nbfh_handle *)NBH_BDEC(pkt->arg[0]);
            if (fh)
                rt->free(rt->ud, fh, NBFH_HANDLE_SIZE);
            res1 = DOSTRUE;
            break;
        }

        case ACTION_INHIBIT:
        case ACTION_IS_FILESYSTEM:
            res1 = DOSTRUE;
            break;

        default:
            res1 = 0;
            res2 = ERROR_ACTION_NOT_KNOWN;
            break;
    }

    pkt->res1 = res1;
    pkt->res2 = res2;
    rt->reply(rt->ud, pkt);
}