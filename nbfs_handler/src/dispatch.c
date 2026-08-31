#include "dispatch.h"
#include <string.h>

static struct nbh_vol g_vol;
static struct nbh_host g_host;
static int g_ready;   /* startup completed */

#define BSTR(n, len, max) \
    do { if ((len) < 0) len = 0; else if ((len) > (max) - 1) len = (max) - 1; \
         p[0] = (uint8_t)(len); memcpy(p + 1, (n), (size_t)(len)); } while (0)

static void fib_fill_file(struct nbh_fib *fib, const struct nbh_ent *e,
                          const char *name, uint32_t name_len,
                          const struct nbh_vol *v, int dir)
{
    uint8_t *p = (uint8_t *)fib;

    memset(fib, 0, NBH_FIB_SIZE);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;   /* fib_DiskKey (LE LONG) */

    /* BSTR file name at fib_FileName (BSTR string: len byte + chars). */
    {
        uint32_t nl = name_len;
        uint8_t *fn = p + NBH_FIB_FILENAME_OFF;
        *fn = (uint8_t)(nl > 107 ? 107 : nl);
        if (nl > 107) nl = 107;
        if (name) memcpy(fn + 1, name, (size_t)nl);
    }
    /* fib_Protection: RWD only (no E bit for either type). Set all set =
     * read/write/delete allowed. */
    p[4 + 12] = 0;
    /* fib_DirEntryType / fib_EntryType offsets: use fixed absolute offsets. */
    {
        /* fib_DirEntryType at offset 4 */
        uint32_t dt = dir ? 2u /* ST_USERDIR */ : 0xFFFFFFFDu /* ST_FILE */;
        p[4] = (uint8_t)(dt & 0xff); p[5] = (uint8_t)((dt >> 8) & 0xff);
        p[6] = (uint8_t)((dt >> 16) & 0xff); p[7] = (uint8_t)((dt >> 24) & 0xff);
        /* fib_EntryType at offset 4+4+108 = 116 */
        {
            uint8_t *et = p + 116;
            et[0] = (uint8_t)(dt & 0xff); et[1] = (uint8_t)((dt >> 8) & 0xff);
            et[2] = (uint8_t)((dt >> 16) & 0xff); et[3] = (uint8_t)((dt >> 24) & 0xff);
        }
    }
    /* fib_Size at offset 120, fib_NumBlocks at 124, fib_Date at 128 */
    {
        uint64_t s = e->size;
        uint8_t *sz = p + 120;
        sz[0] = (uint8_t)(s & 0xff); sz[1] = (uint8_t)((s >> 8) & 0xff);
        sz[2] = (uint8_t)((s >> 16) & 0xff); sz[3] = (uint8_t)((s >> 24) & 0xff);
        {
            uint64_t nb = (s + 4095) / 4096;
            uint8_t *bn = p + 124;
            bn[0] = (uint8_t)(nb & 0xff); bn[1] = (uint8_t)((nb >> 8) & 0xff);
            bn[2] = (uint8_t)((nb >> 16) & 0xff); bn[3] = (uint8_t)((nb >> 24) & 0xff);
            (void)v;
        }
    }
}

static void pkt_err(long *res1, long *res2, long error)
{
    *res1 = DOSFALSE;
    *res2 = error;
}

void nbh_dispatch(struct nbh_pkt *pkt, long *res1, long *res2)
{
    struct nbh_vol *v = &g_vol;

    *res1 = DOSFALSE;
    *res2 = 0;

    switch (pkt->type) {

    case ACTION_STARTUP:
        g_ready = 1;
        *res1 = DOSTRUE;
        break;

    case ACTION_DISK_INFO:
    case ACTION_GET_DISK_INFO:
    {
        struct nbh_info *id = (struct nbh_info *)NBH_PTR(pkt->arg1);
        uint8_t *p = (uint8_t *)id;
        int i;
        memset(id, 0, sizeof(*id));
        p[12] = 0; p[13] = 0; p[14] = 0; p[15] = 0;   /* diskState */
        /* numBlocks at 16 */
        {
            uint64_t tb = v->total_blocks;
            p[16] = (uint8_t)(tb & 0xff); p[17] = (uint8_t)((tb >> 8) & 0xff);
            p[18] = (uint8_t)((tb >> 16) & 0xff); p[19] = (uint8_t)((tb >> 24) & 0xff);
        }
        /* numBlocksUsed at 20 */
        {
            uint64_t u = v->total_blocks - v->free_blocks;
            p[20] = (uint8_t)(u & 0xff); p[21] = (uint8_t)((u >> 8) & 0xff);
            p[22] = (uint8_t)((u >> 16) & 0xff); p[23] = (uint8_t)((u >> 24) & 0xff);
        }
        /* bytesPerBlock at 24, diskType at 28 */
        {
            uint32_t bpb = v->block_size;
            p[24] = (uint8_t)(bpb & 0xff); p[25] = (uint8_t)((bpb >> 8) & 0xff);
            p[26] = (uint8_t)((bpb >> 16) & 0xff); p[27] = (uint8_t)((bpb >> 24) & 0xff);
            p[28] = (uint8_t)(0x4e & 0xff); p[29] = (uint8_t)(0x42 & 0xff); /* 'N','B' */
            p[30] = (uint8_t)(0x46 & 0xff); p[31] = (uint8_t)(0x53 & 0xff);
        }
        /* volumeName BSTR at id_VolumeName offset 36+4 = 40 */
        {
            const char *vn = v->volume_name;
            int vl = (int)strlen(vn);
            BSTR(vn, vl, 36);
            p = (uint8_t *)id + 40;
        }
        *res1 = DOSTRUE;
        break;
    }

    case ACTION_LOCATE_OBJECT:
    {
        struct nbh_filelock *fl = NULL;
        struct nbh_ent dir, e;
        const uint8_t *nameb = NULL;
        uint32_t name_len = 0;
        const char *path = "";
        int32_t access = (int32_t)(uintptr_t)NBH_PTR(pkt->arg3);
        uint32_t parent_ino = 0;
        int r;

        if (pkt->arg1 != 0) {
            struct nbh_filelock *pl = (struct nbh_filelock *)NBH_PTR(pkt->arg1);
            parent_ino = pl->fl_Key;
        }
        if (pkt->arg2 != 0) {
            nameb = (const uint8_t *)NBH_PTR(pkt->arg2);
            name_len = nameb[0];
            path = (const char *)(nameb + 1);
        }

        /* Resolve parent: default root. */
        if (parent_ino == 0 || parent_ino == (uint32_t)v->root_ino)
            r = nbh_read_inode(v, v->root_ino, &dir);
        else
            r = nbh_read_inode(v, parent_ino, &dir);
        if (r != 0 || !(dir.mode & NBH_MODE_DIRECTORY)) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }

        /* "." or "" -> root.  ".." unsupported (NBFS has no parent links). */
        if (name_len == 0 || (name_len == 1 && path[0] == '.')) {
            r = nbh_read_inode(v, v->root_ino, &e);
            path = "ROOT";
        } else if (name_len == 2 && path[0] == '.' && path[1] == '.') {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        } else {
            char nm[NBH_MAX_NAME_LEN + 1];
            unsigned i;
            if (name_len > NBH_MAX_NAME_LEN)
                name_len = NBH_MAX_NAME_LEN;
            for (i = 0; i < name_len; i++)
                nm[i] = (char)path[i];
            nm[name_len] = 0;
            if (strchr(nm, '/') || strchr(nm, ':')) {
                pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
                break;
            }
            r = nbh_dirfind(v, &dir, nm, &e);
            if (r != 0) {
                pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
                break;
            }
            path = nm;
        }
        if (r != 0) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        if (!g_host.alloc_lock(&fl, (uint32_t)e.ino, access)) {
            pkt_err(res1, res2, -113); /* ERROR_DEVICE_NOT_MOUNTED */
            break;
        }
        *res1 = NBH_PTR_LONG(fl);
        *res2 = 0;
        break;
    }

    case ACTION_CURRENT_DIR:
    {
        /* Arg1=0 -> return a lock on root; Arg1=lock -> echo it. */
        if (pkt->arg1 != 0) {
            *res1 = NBH_PTR_LONG(NBH_PTR(pkt->arg1));
        } else {
            struct nbh_filelock *fl = NULL;
            if (!g_host.alloc_lock(&fl, (uint32_t)v->root_ino, 0))
                break;
            *res1 = NBH_PTR_LONG(fl);
        }
        *res2 = 0;
        break;
    }

    case ACTION_PARENT:
    {
        /* NBFS has no parent links; fail. */
        pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
        break;
    }

    case ACTION_FREE_LOCK:
    {
        struct nbh_filelock *fl = (struct nbh_filelock *)NBH_PTR(pkt->arg1);
        if (fl)
            g_host.free_lock(fl);
        *res1 = DOSTRUE;
        break;
    }

    case ACTION_EXAMINE_OBJECT:
    {
        struct nbh_filelock *fl = (struct nbh_filelock *)NBH_PTR(pkt->arg1);
        struct nbh_fib *fib = (struct nbh_fib *)NBH_PTR(pkt->arg2);
        struct nbh_ent e;
        if (!fl || !fib) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        if (nbh_read_inode(v, fl->fl_Key, &e) != 0) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        if (e.mode & NBH_MODE_DIRECTORY) {
            fib_fill_file(fib, &e, v->volume_name,
                          (uint32_t)strlen(v->volume_name), v, 1);
            fl->fl_Arg1 = 1;   /* scan starts at ordinal 1 */
        } else {
            fib_fill_file(fib, &e, "", 0, v, 0);
            fl->fl_Arg1 = 0;
        }
        *res1 = DOSTRUE;
        break;
    }

    case ACTION_EXAMINE_NEXT:
    {
        struct nbh_filelock *fl = (struct nbh_filelock *)NBH_PTR(pkt->arg1);
        struct nbh_fib *fib = (struct nbh_fib *)NBH_PTR(pkt->arg2);
        struct nbh_ent dir, e;
        char name[NBH_MAX_NAME_LEN + 1];
        uint64_t k;
        int r;

        if (!fl || !fib) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        k = fl->fl_Arg1;
        if (k == 0)
            k = 1;
        if (nbh_read_inode(v, fl->fl_Key, &dir) != 0 ||
            !(dir.mode & NBH_MODE_DIRECTORY)) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        r = nbh_direnum(v, &dir, k, &e, name);
        if (r < 0) {
            pkt_err(res1, res2, ERROR_READ_PROTECTED);
            break;
        }
        if (r > 0) {
            /* end of directory (EX_END) */
            *res1 = DOSFALSE;
            *res2 = ERROR_NO_MORE_ENTRIES;
            break;
        }
        fib_fill_file(fib, &e, name, (uint32_t)strlen(name), v,
                      (e.mode & NBH_MODE_DIRECTORY) != 0);
        fl->fl_Arg1 = (uint32_t)(k + 1);
        *res1 = DOSTRUE;
        break;
    }

    case ACTION_OPEN:
    case ACTION_FINDINPUT:
    case ACTION_FINDOUTPUT:
    {
        struct nbh_fh *fh = (struct nbh_fh *)NBH_PTR(pkt->arg1);
        (void)fh;
        /* arg1 = FileHandle*, arg2 = parent lock, arg3 = name BSTR.
         * Amiga glue resolved the inode into fh->fh_Arg1. */
        if (fh && fh->fh_Arg1 != 0) {
            fh->fh_Pos = 0;
            fh->fh_End = 0;
            *res1 = DOSTRUE;
        } else {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
        }
        break;
    }

    case ACTION_CLOSE:
    case ACTION_END:
        *res1 = DOSTRUE;
        break;

    case ACTION_READ:
    {
        struct nbh_fh *fh = (struct nbh_fh *)NBH_PTR(pkt->arg1);
        void *dst = NBH_PTR(pkt->arg2);
        uint32_t len = (uint32_t)(uintptr_t)NBH_PTR(pkt->arg3);
        struct nbh_ent e;
        int64_t n;
        if (!fh || fh->fh_Arg1 == 0) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        if (nbh_read_inode(v, fh->fh_Arg1, &e) != 0) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        n = nbh_read(v, &e, (uint64_t)fh->fh_Pos, dst, len);
        if (n < 0) {
            pkt_err(res1, res2, ERROR_READ_PROTECTED);
            break;
        }
        fh->fh_Pos += (int32_t)n;
        *res1 = n;
        break;
    }

    case ACTION_SEEK:
    {
        struct nbh_fh *fh = (struct nbh_fh *)NBH_PTR(pkt->arg1);
        int32_t offset = (int32_t)(uintptr_t)NBH_PTR(pkt->arg2);
        int32_t mode = (int32_t)(uintptr_t)NBH_PTR(pkt->arg3);
        int64_t newpos;
        if (!fh || fh->fh_Arg1 == 0) {
            pkt_err(res1, res2, ERROR_OBJECT_NOT_FOUND);
            break;
        }
        if (mode == 0)           newpos = offset;
        else if (mode == 1)      newpos = (int64_t)fh->fh_Pos + offset;
        else if (mode == 2) {
            struct nbh_ent e;
            newpos = 0;
            if (nbh_read_inode(v, fh->fh_Arg1, &e) == 0)
                newpos = (int64_t)e.size + offset;
            if (newpos < 0)
                pkt_err(res1, res2, ERROR_SEEK_ERROR - 0);
        } else {
            pkt_err(res1, res2, ERROR_SEEK_ERROR - 0);
            break;
        }
        if (newpos < 0) {
            pkt_err(res1, res2, ERROR_SEEK_ERROR - 0);
            break;
        }
        fh->fh_Pos = (int32_t)newpos;
        *res1 = fh->fh_Pos;
        break;
    }

    default:
        /* Writes etc.: not supported on read-only medium. */
        pkt_err(res1, res2, ERROR_READ_PROTECTED);
        break;
    }
}

void nbh_dispatch_init(struct nbh_host *host, struct nbh_vol *vol)
{
    if (host)
        g_host = *host;
    if (vol)
        g_vol = *vol;
    g_ready = 0;
}