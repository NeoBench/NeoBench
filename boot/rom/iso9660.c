/*
 * ISO 9660, read through atapi.device (iso9660.h).
 *
 * The whole disc arrives one 2048-byte sector at a time, so this is
 * built around three small facts of the format: the boot block is at
 * sector 0, the volume descriptor at sector 16, and a directory is a
 * run of variable-length records with zero fill to the end of each
 * sector.  Nothing here allocates or remembers a whole table -- a
 * lookup re-reads the directory it is walking -- because boot is the
 * only time this runs and the disc under the reader is the slowest
 * thing in the machine anyway.
 *
 * The record layouts are the ECMA-119 ones, and the two that look
 * alike are worth repeating: a *directory record* leads with its
 * length byte then the extent, while a *path table record* leads with
 * its identifier length.  This file only ever walks directory
 * records; the path tables exist for other readers and are not
 * consulted here.
 */
#include <stdint.h>

#include "amiga.h"
#include "dev.h"
#include "iso9660.h"

#define SECTOR 2048u

static struct
{
    int      state;      /* NB_ISO_NONE / DISC / INSTALL            */
    char     vol[33];    /* volume identity, spaces trimmed, NUL'd  */
    unsigned files;      /* files in the root directory             */
    uint32_t root_lba;   /* root directory extent, in CD sectors    */
    uint32_t root_len;   /* root directory length, in bytes         */
} iso;                   /* .bss: zero before the first mount       */

static uint8_t scratch[SECTOR];   /* .bss: the streaming read window */

static void s_put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void s_u(unsigned v)
{
    char b[11];
    int i = 11;

    b[--i] = '\0';
    do
    {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    s_put(&b[i]);
}

static int cd_read(uint32_t lba, uint8_t *buf)
{
    return nb_dev_read("atapi.device", 0, lba, buf, 1);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int cdcopy(void *dst, const uint8_t *src, unsigned n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n--)
        *d++ = *src++;
    return 1;
}

/*
 * One name against one record identifier: case does not matter and
 * neither does the ";1" version, so a caller may ask for exactly the
 * name it staged and the disc may answer in whatever case ISO 9660
 * level 1 forced it into.
 */
static int name_match(const char *name, const uint8_t *rec, unsigned nlen)
{
    unsigned i;

    for (i = 0; name[i]; i++)
    {
        unsigned char a = (unsigned char)name[i];
        unsigned char b;

        if (i >= nlen)
            return 0;
        b = rec[i];
        if (a >= 'a' && a <= 'z')
            a = (unsigned char)(a - 32);
        if (b >= 'a' && b <= 'z')
            b = (unsigned char)(b - 32);
        if (a != b)
            return 0;
    }
    return i == nlen || rec[i] == ';';
}

/*
 * Walk one directory for one name.  `pos' steps record by record and
 * the zero byte at a sector's tail is the cue to jump to the next
 * one, which is how the standard says a directory ends when the last
 * record did not reach the edge.
 */
static int find_in(uint32_t dir_lba, uint32_t dir_len, const char *name,
                   uint32_t *ext, uint32_t *len, uint8_t *flags)
{
    uint8_t sec[SECTOR];
    uint32_t pos = 0;

    while (pos + 33 <= dir_len)
    {
        unsigned off = (unsigned)(pos % SECTOR);
        unsigned reclen, nlen;
        const uint8_t *nm;

        if (!cd_read(dir_lba + pos / SECTOR, sec))
            return 0;
        if (sec[off] == 0)
        {
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }
        reclen = sec[off];
        if (off + reclen > SECTOR)
            return 0;               /* a record crossing a sector edge */
        nlen = sec[off + 32];
        nm = sec + off + 33;

        /* "." and ".." carry single-byte identifiers 0 and 1. */
        if (!(nlen == 1 && (nm[0] == 0 || nm[0] == 1)) &&
            name_match(name, nm, nlen))
        {
            *ext = le32(sec + off + 2);
            *len = le32(sec + off + 10);
            *flags = sec[off + 25];
            return 1;
        }
        pos += reclen;
    }
    return 0;
}

/* Files, not directories, in the root: what the boot log reports. */
static unsigned count_files(uint32_t dir_lba, uint32_t dir_len)
{
    uint8_t sec[SECTOR];
    uint32_t pos = 0;
    unsigned n = 0;

    while (pos + 33 <= dir_len)
    {
        unsigned off = (unsigned)(pos % SECTOR);
        unsigned reclen, nlen;
        const uint8_t *nm;

        if (!cd_read(dir_lba + pos / SECTOR, sec))
            return n;
        if (sec[off] == 0)
        {
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }
        reclen = sec[off];
        if (off + reclen > SECTOR)
            return n;
        nlen = sec[off + 32];
        nm = sec + off + 33;
        if (!(nlen == 1 && (nm[0] == 0 || nm[0] == 1)) &&
            !(sec[off + 25] & 2u))
            n++;
        pos += reclen;
    }
    return n;
}

int nb_iso_mount(void)
{
    uint8_t sec[SECTOR];
    int boot = 0;
    unsigned i;

    iso.state = NB_ISO_NONE;
    iso.vol[0] = '\0';
    iso.files = 0;
    iso.root_lba = 0;
    iso.root_len = 0;

    /* Sector 0: NeoBench's installer boot block, if this is our disc. */
    if (cd_read(0, sec) &&
        sec[0] == 'N' && sec[1] == 'B' && sec[2] == 'I' &&
        sec[3] == 'S' && sec[4] == 'O' && sec[5] == 1)
        boot = 1;

    if (!cd_read(16, sec) || sec[0] != 1 ||
        sec[1] != 'C' || sec[2] != 'D' || sec[3] != '0' ||
        sec[4] != '0' || sec[5] != '1' || sec[6] != 1)
    {
        s_put(">iso state=0 boot=");
        s_u(boot);
        amiga_serial_putc('\r');
        amiga_serial_putc('\n');
        return NB_ISO_NONE;         /* no media, or not ISO 9660     */
    }

    for (i = 0; i < 32 && sec[40 + i] != ' ' && sec[40 + i] != '\0'; i++)
        iso.vol[i] = (char)sec[40 + i];
    iso.vol[i] = '\0';

    if (sec[156] < 34)
    {
        iso.state = NB_ISO_NONE;
        return NB_ISO_NONE;         /* no root record, no directory  */
    }
    iso.root_lba = le32(sec + 156 + 2);
    iso.root_len = le32(sec + 156 + 10);
    iso.state = boot ? NB_ISO_INSTALL : NB_ISO_DISC;
    iso.files = count_files(iso.root_lba, iso.root_len);

    s_put(">iso state=");
    s_u((unsigned)iso.state);
    s_put(" boot=");
    s_u((unsigned)boot);
    s_put(" vol=");
    s_put(iso.vol);
    s_put(" files=");
    s_u(iso.files);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
    return iso.state;
}

const char *nb_iso_volume(void)
{
    return iso.vol;
}

unsigned nb_iso_files(void)
{
    return iso.files;
}

int nb_iso_stat(const char *path, uint32_t *lba, uint32_t *size)
{
    uint32_t dir_lba, dir_len, ext, len;
    uint8_t flags = 0;
    char comp[64];
    unsigned ci = 0;

    if (iso.state == NB_ISO_NONE || !path || !lba || !size)
        return 0;
    dir_lba = iso.root_lba;
    dir_len = iso.root_len;

    for (;;)
    {
        char c = *path++;
        int last;

        if (c == '/' || c == '\0')
        {
            if (ci == 0)
            {
                if (c == '\0')
                    return 0;       /* "//" or a trailing slash      */
                continue;           /* ignore a leading '/'          */
            }
            comp[ci] = '\0';
            ci = 0;
            last = (c == '\0');
            if (!find_in(dir_lba, dir_len, comp, &ext, &len, &flags))
                return 0;
            if (last)
            {
                if (flags & 2u)     /* a directory is not a file     */
                    return 0;
                *lba = ext;
                *size = len;
                return 1;
            }
            if (!(flags & 2u))
                return 0;           /* a file cannot be descended to */
            dir_lba = ext;
            dir_len = len;
            continue;
        }
        if (ci >= sizeof comp - 1)
            return 0;
        comp[ci++] = c;
    }
}

int nb_iso_read(uint32_t lba, unsigned offset, void *dst, unsigned len)
{
    uint8_t *p = (uint8_t *)dst;
    uint32_t sec = lba + offset / SECTOR;
    unsigned skip = offset % SECTOR;

    while (len)
    {
        unsigned n = SECTOR - skip;

        if (n > len)
            n = len;
        if (!cd_read(sec, scratch))
            return 0;
        cdcopy(p, scratch + skip, n);
        p += n;
        len -= n;
        sec++;
        skip = 0;
    }
    return 1;
}
