#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dospackets.h"

static FILE *g_img;
static int g_fails;

static int memread(void *ud, uint32_t sector, uint32_t count, void *buf)
{
    (void)ud;
    if (fseek(g_img, (long)sector * NBH_SECTOR_SIZE, SEEK_SET) != 0)
        return -1;
    if (fread(buf, NBH_SECTOR_SIZE, count, g_img) != count)
        return -1;
    return 0;
}

static void *h_alloc(void *ud, uint32_t size)
{
    (void)ud;
    return calloc(1, size);
}

static void h_free(void *ud, void *p, uint32_t size)
{
    (void)ud;
    (void)size;
    free(p);
}

static void h_reply(void *ud, struct nbfh_pkt *pkt)
{
    (void)ud;
    (void)pkt;
}

#define CHECK(cond, ...)                                  \
    do                                                    \
    {                                                     \
        if (!(cond))                                      \
        {                                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            g_fails++;                                    \
        }                                                 \
    } while (0)

static struct nbfh_fib g_fib;
static struct nbfh_info g_info;
static struct nbfh_pkt g_pkt;
static struct nbh_vol g_vol;
static struct nbh_rt g_rt;

static uintptr_t locate(const char *name)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_LOCATE_OBJECT;
    g_pkt.arg[0] = (uintptr_t)name;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
    return (uintptr_t)g_pkt.res1;
}

static int examine_obj(uintptr_t lock)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_EXAMINE_OBJECT;
    g_pkt.arg[0] = lock;
    g_pkt.arg[1] = (uintptr_t)&g_fib;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
    return (int)g_pkt.res1;
}

static int examine_next(uintptr_t lock)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_EXAMINE_NEXT;
    g_pkt.arg[0] = lock;
    g_pkt.arg[1] = (uintptr_t)&g_fib;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
    return (int)g_pkt.res1;
}

static uintptr_t findinput(uintptr_t lock)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_FINDINPUT;
    g_pkt.arg[0] = lock;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
    return (uintptr_t)g_pkt.res1;
}

static int read_at(uintptr_t fh, uint8_t *buf, int32_t want)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_READ;
    g_pkt.arg[0] = fh;
    g_pkt.arg[1] = (uintptr_t)buf;
    g_pkt.arg[2] = (uintptr_t)want;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
    return (int)g_pkt.res1;
}

static void seek_end(uintptr_t fh, int32_t off)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_SEEK;
    g_pkt.arg[0] = fh;
    g_pkt.arg[1] = (uintptr_t)off;
    g_pkt.arg[2] = (uintptr_t)OFFSET_END;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
}

static void end_file(uintptr_t fh)
{
    memset(&g_pkt, 0, sizeof(g_pkt));
    g_pkt.type = ACTION_END;
    g_pkt.arg[0] = fh;
    nbh_serve(&g_vol, &g_rt, &g_pkt);
}

static void read_file_checked(const char *path, const void *expect,
                              size_t expect_len)
{
    uintptr_t lk = locate(path);
    struct nbfh_lock *lock = (struct nbfh_lock *)lk;
    struct nbfh_handle *fh;
    uint8_t *buf;
    int got;

    if (!lk)
    {
        CHECK(0, "locate %s (res2=%u)", path, (unsigned)g_pkt.res2);
        return;
    }
    CHECK(examine_obj(lk) == DOSTRUE, "examine %s", path);
    CHECK(g_fib.entrytype == ST_FILE, "%s is file", path);
    CHECK(g_fib.fsize == expect_len, "%s size %u != %zu", path, g_fib.fsize,
          expect_len);

    fh = (struct nbfh_handle *)findinput(lk);
    CHECK(fh != NULL, "findinput %s", path);
    if (fh)
    {
        CHECK(fh->end == (int32_t)expect_len, "%s fh end", path);
        buf = malloc(expect_len + 64);
        got = read_at((uintptr_t)fh, buf, (int32_t)expect_len + 64);
        CHECK(got == (int)expect_len, "%s read all: got %d want %zu", path,
              got, expect_len);
        CHECK(memcmp(buf, expect, expect_len) == 0, "%s content", path);

        seek_end((uintptr_t)fh, -(int32_t)expect_len);
        CHECK(g_pkt.res1 == DOSTRUE, "%s seek to 0", path);
        got = read_at((uintptr_t)fh, buf, (int32_t)expect_len);
        CHECK(got == (int)expect_len, "%s reread", path);
        CHECK(memcmp(buf, expect, expect_len) == 0, "%s content 2", path);

        end_file((uintptr_t)fh);
        free(buf);
    }
    free(lock);
}

int main(int argc, char **argv)
{
    const char *imgpath =
        argc > 1 ? argv[1] : "tests/fixture/fixture.nbfs";
    uintptr_t lk;
    char *entries[32];
    uint32_t nentries = 0;

    g_img = fopen(imgpath, "rb");
    if (!g_img)
    {
        printf("cannot open %s\n", imgpath);
        return 1;
    }

    memset(&g_vol, 0, sizeof(g_vol));
    g_vol.io.ud = NULL;
    g_vol.io.read = memread;
    if (nbh_mount(&g_vol) != 0)
    {
        printf("mount failed\n");
        return 1;
    }
    printf("volname=%s blocks=%u free=%u inodes=%u/%u root=%u itable=%u data=%u\n",
           g_vol.volname, g_vol.total_blocks, g_vol.free_blocks,
           g_vol.total_inodes, g_vol.free_inodes, g_vol.root_inode,
           g_vol.itable_start, g_vol.data_start);

    memset(&g_rt, 0, sizeof(g_rt));
    g_rt.ud = NULL;
    g_rt.handler_port = 0x01020304;
    g_rt.alloc = h_alloc;
    g_rt.free = h_free;
    g_rt.reply = h_reply;

    CHECK(strcmp(g_vol.volname, "FIXTURE") == 0, "volname = FIXTURE");

    /* Root lock + examine */
    lk = locate("");
    {
        struct nbfh_lock *lock = (struct nbfh_lock *)lk;
        CHECK(lock != NULL, "locate root");
        if (!lock)
            return 1;
        CHECK(examine_obj(lk) == DOSTRUE, "examine root");
        CHECK(g_fib.dirdtype == ST_USERDIR, "root dirdtype=%ld",
              (long)g_fib.dirdtype);
        CHECK(g_fib.filename_len > 0 &&
                  strcmp(g_vol.volname, g_fib.filename) == 0,
              "root name '%s' != volname '%s'", g_fib.filename,
              g_vol.volname);
        CHECK(lock->task == 0x01020304, "lock task = handler port");
    }

    /* List root: expect etc, docs, README.txt, flag.txt (no ./..) */
    fprintf(stderr, "-- listing root --\n");
    {
        struct nbfh_fib fib_c;
        memset(&fib_c, 0, sizeof(fib_c));
        do
        {
            memset(&g_pkt, 0, sizeof(g_pkt));
            g_pkt.type = ACTION_EXAMINE_NEXT;
            g_pkt.arg[0] = lk;
            g_pkt.arg[1] = (uintptr_t)&fib_c;
            nbh_serve(&g_vol, &g_rt, &g_pkt);
            if (g_pkt.res1 == DOSTRUE)
            {
                char nm[120];
                snprintf(nm, sizeof(nm), "%s%s", fib_c.filename,
                         fib_c.entrytype == ST_USERDIR ? "/" : "");
                fprintf(stderr, "  %s (%s, %u B)\n", nm,
                        fib_c.entrytype == ST_USERDIR ? "dir" : "file",
                        fib_c.fsize);
                CHECK(fib_c.filename_len > 0, "entry has name");
                CHECK(strcmp(fib_c.filename, ".") != 0 &&
                          strcmp(fib_c.filename, "..") != 0,
                      "no dot entries");
                entries[nentries] = strdup(fib_c.filename);
                nentries++;
                CHECK(nentries <= 32, "too many entries");
            }
            else
            {
                CHECK(g_pkt.res2 == ERROR_NO_MORE_ENTRIES, "list end res2=%u",
                      (unsigned)g_pkt.res2);
                break;
            }
        } while (1);
    }

    if (nentries == 4)
    {
        CHECK(strcmp(entries[0], "etc") == 0, "entry[0]=etc");
        CHECK(strcmp(entries[1], "docs") == 0, "entry[1]=docs");
        CHECK(strcmp(entries[2], "README.txt") == 0, "entry[2]=README.txt");
        CHECK(strcmp(entries[3], "flag.txt") == 0, "entry[3]=flag.txt");
    }
    else
    {
        CHECK(0, "root listing count %u", nentries);
    }
    for (uint32_t i = 0; i < nentries; i++)
        free(entries[i]);

    /* Missing objects */
    {
        lk = locate("nope.txt");
        CHECK(lk == 0 && g_pkt.res2 == ERROR_OBJECT_NOT_FOUND,
              "locate nope -> 205");
        lk = locate("etc/nope");
        CHECK(lk == 0 && g_pkt.res2 == ERROR_OBJECT_NOT_FOUND,
              "locate etc/nope -> 205");
        lk = locate("..");
        CHECK(lk == 0 && g_pkt.res2 == ERROR_OBJECT_NOT_FOUND,
              "locate '..' -> 205");
    }

    /* Read files */
    read_file_checked("flag.txt", "NBFS-HANDLER-FLAG\n", 18);
    read_file_checked("etc/motd", "Greetings from NBFS.\n", 21);
    read_file_checked("etc/version", "vT-1\n", 5);
    {
        char docs[512];
        size_t dl = 0;
        for (int i = 0; i < 5; i++)
        {
            size_t each = strlen("Docs for the handler test.\n");
            memcpy(docs + dl, "Docs for the handler test.\n", each);
            dl += each;
        }
        read_file_checked("docs/readme.txt", docs, dl);
    }

    /* Directory locks examine + subdir listing */
    {
        uintptr_t e = locate("etc");
        struct nbfh_lock *el = (struct nbfh_lock *)e;
        struct nbfh_fib fib_c;
        CHECK(el != NULL, "locate etc");
        if (el)
        {
            CHECK(examine_obj(e) == DOSTRUE, "examine etc");
            CHECK(g_fib.dirdtype == ST_USERDIR, "etc is dir");
            memset(&fib_c, 0, sizeof(fib_c));
            memset(&g_pkt, 0, sizeof(g_pkt));
            g_pkt.type = ACTION_EXAMINE_NEXT;
            g_pkt.arg[0] = e;
            g_pkt.arg[1] = (uintptr_t)&fib_c;
            nbh_serve(&g_vol, &g_rt, &g_pkt);
            CHECK(g_pkt.res1 == DOSTRUE, "etc first entry");
            CHECK(strcmp(fib_c.filename, "motd") == 0, "etc[0]=motd");
            memset(&g_pkt, 0, sizeof(g_pkt));
            g_pkt.type = ACTION_EXAMINE_NEXT;
            g_pkt.arg[0] = e;
            g_pkt.arg[1] = (uintptr_t)&fib_c;
            nbh_serve(&g_vol, &g_rt, &g_pkt);
            CHECK(g_pkt.res1 == DOSTRUE, "etc second entry");
            CHECK(strcmp(fib_c.filename, "version") == 0, "etc[1]=version");
            memset(&g_pkt, 0, sizeof(g_pkt));
            g_pkt.type = ACTION_EXAMINE_NEXT;
            g_pkt.arg[0] = e;
            g_pkt.arg[1] = (uintptr_t)&fib_c;
            nbh_serve(&g_vol, &g_rt, &g_pkt);
            CHECK(g_pkt.res1 == 0 && g_pkt.res2 == ERROR_NO_MORE_ENTRIES,
                  "etc end");
            free(el);
        }
    }

    /* DISK_INFO */
    {
        memset(&g_pkt, 0, sizeof(g_pkt));
        g_pkt.type = ACTION_DISK_INFO;
        g_pkt.arg[0] = (uintptr_t)&g_info;
        nbh_serve(&g_vol, &g_rt, &g_pkt);
        CHECK(g_pkt.res1 == DOSTRUE, "disk_info res1");
        CHECK(g_info.disk_state == ID_WRITE_PROTECTED, "disk_state wp");
        CHECK(g_info.num_blocks == g_vol.total_blocks, "disk blocks");
        CHECK(g_info.bytes_per_block == (int32_t)NBH_BLOCK_SIZE,
              "disk bpb");
        printf("disk: blocks=%u used=%u type=0x%x bpb=%ld\n",
               g_info.num_blocks, g_info.num_blocks_used, g_info.disk_type,
               (long)g_info.bytes_per_block);
    }

    /* Write protection */
    {
        uintptr_t f = locate("flag.txt");
        struct nbfh_lock *fl = (struct nbfh_lock *)f;
        struct nbfh_handle *fh;
        if (fl)
        {
            fh = (struct nbfh_handle *)findinput(f);
            if (fh)
            {
                memset(&g_pkt, 0, sizeof(g_pkt));
                g_pkt.type = ACTION_WRITE;
                g_pkt.arg[0] = (uintptr_t)fh;
                g_pkt.arg[1] = (uintptr_t)"abc";
                g_pkt.arg[2] = 3;
                nbh_serve(&g_vol, &g_rt, &g_pkt);
                CHECK(g_pkt.res1 == 0 &&
                          g_pkt.res2 == ERROR_WRITE_PROTECTED,
                      "write -> 223");

                memset(&g_pkt, 0, sizeof(g_pkt));
                g_pkt.type = ACTION_FINDOUTPUT;
                g_pkt.arg[0] = f;
                nbh_serve(&g_vol, &g_rt, &g_pkt);
                CHECK(g_pkt.res1 == 0 &&
                          g_pkt.res2 == ERROR_WRITE_PROTECTED,
                      "findoutput -> 223");
            }
            free(fl);
        }
    }

    fclose(g_img);
    printf(g_fails ? "FAILED (%d)\n" : "ALL OK\n", g_fails);
    return g_fails ? 1 : 0;
}