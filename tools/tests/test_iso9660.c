/*
 * Host test of the install disc: tools/mkiso.py, boot/rom/iso9660.c
 * and boot/rom/install.c, run against each other.
 *
 * Nothing about a CD can be tested in the emulator until a CD is
 * attached to it, and nothing about the reader can be tested then
 * without a disc that already has a fault in it -- so the disc is
 * built here by the same two tools that build the real one (the
 * fixture rules in the Makefile run mknbfs.py and mkiso.py), and
 * behind two fake devices registered in the device table under their
 * real names: atapi.device reading the ISO image, ata.device a RAM
 * disk.  The code under test sees exactly what it sees on the
 * machine -- sectors arriving through nb_dev_read -- and the test
 * gets to check every branch, including the ones a first boot on
 * real hardware will never take:
 *
 *   - mount, volume identity, root listing, path lookup in either
 *     case, file bytes across a sector boundary;
 *   - install refused when there is no disk;
 *   - install onto a blank disk, and the bytes that leave behind;
 *   - install recognising its own earlier work;
 *   - install refusing a disk that holds someone else's data;
 *   - install declining a damaged NeoBench volume.
 *
 *   cc -O2 -Wall -Wextra -o test_iso9660 test_iso9660.c \
 *      ../../boot/rom/iso9660.c ../../boot/rom/install.c \
 *      ../../boot/rom/dev.c
 *   ./test_iso9660            (after the fixture rules have run)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../boot/rom/amiga.h"
#include "../../boot/rom/dev.h"
#include "../../boot/rom/install.h"
#include "../../boot/rom/iso9660.h"

#define CD_SEC 2048u
#define HD_SEC 512u

static int failures;

static void check(int ok, const char *what)
{
    if (!ok)
    {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void check_u(unsigned got, unsigned want, const char *what)
{
    if (got != want)
    {
        failures++;
        printf("FAIL: %s: got %u, want %u\n", what, got, want);
    }
}

/*
 * The serial port the modules under test write their ">" lines to:
 * one line is kept, so a test can check that the line exists and says
 * what it should immediately after the call that produces it.
 */
static char last_line[160];
static char building[160];
static unsigned blen;

void amiga_serial_putc(char c)
{
    if (c == '\r')
        return;
    if (c == '\n')
    {
        building[blen] = '\0';
        strcpy(last_line, building);
        blen = 0;
        building[0] = '\0';
        return;
    }
    if (blen < sizeof building - 1)
        building[blen++] = c;
}

/* ---- the fake CD: atapi.device unit 0 over the ISO image ---------- */

static uint8_t *iso_img;
static size_t iso_len;

static int cd_read(unsigned unit, uint32_t lba, void *dst, unsigned count)
{
    uint8_t *d = (uint8_t *)dst;
    unsigned i;

    if (unit != 0)
        return 0;
    for (i = 0; i < count; i++)
    {
        size_t off = ((size_t)lba + i) * CD_SEC;

        if (off + CD_SEC > iso_len)
            return 0;
        memcpy(d + (size_t)i * CD_SEC, iso_img + off, CD_SEC);
    }
    return 1;
}

/* ---- the fake disk: ata.device unit 0 over a RAM disk ------------- */

static uint8_t *disk;
static size_t disk_len;

static int hd_read(unsigned unit, uint32_t lba, void *dst, unsigned count)
{
    size_t off = (size_t)lba * HD_SEC;

    if (unit != 0 || off + (size_t)count * HD_SEC > disk_len)
        return 0;
    memcpy(dst, disk + off, (size_t)count * HD_SEC);
    return 1;
}

static int hd_write(unsigned unit, uint32_t lba, const void *src,
                    unsigned count)
{
    size_t off = (size_t)lba * HD_SEC;

    if (unit != 0 || off + (size_t)count * HD_SEC > disk_len)
        return 0;
    memcpy(disk + off, src, (size_t)count * HD_SEC);
    return 1;
}

static uint8_t *load(const char *path, size_t *len)
{
    FILE *fh = fopen(path, "rb");
    long n;
    uint8_t *buf;

    if (!fh)
    {
        printf("FAIL: cannot open %s (run make first)\n", path);
        failures++;
        *len = 0;
        return NULL;
    }
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fh) != (size_t)n)
    {
        printf("FAIL: short read of %s\n", path);
        failures++;
        free(buf);
        fclose(fh);
        *len = 0;
        return NULL;
    }
    fclose(fh);
    *len = (size_t)n;
    return buf;
}

/* ---- the checks --------------------------------------------------- */

static void test_mount(void)
{
    check_u((unsigned)nb_iso_mount(), NB_ISO_INSTALL,
            "mount: the fixture is NeoBench's installer disc");
    check(strcmp(nb_iso_volume(), "NEOBENCH") == 0,
          "volume identity as staged");
    /* INSTALL.TXT, NBFS.IMG -- DOCS is a directory and does not count */
    check_u(nb_iso_files(), 2, "root holds two files");
    check(strstr(last_line, ">iso state=2") == last_line,
          "serial: >iso state=2");
    check(strstr(last_line, "vol=NEOBENCH") != NULL,
          "serial: the volume is on the line");
}

static void test_lookup(void)
{
    uint32_t lba = 0, size = 0;
    uint8_t buf[64];

    check(nb_iso_stat("INSTALL.TXT", &lba, &size) && size > 0,
          "root file found");
    check(strstr(last_line, ">iso") == last_line, /* unchanged */
          "stat does not disturb the mount line");

    check(nb_iso_stat("INSTALL.TXT", &lba, &size) &&
          nb_iso_read(lba, 0, buf, 24) &&
          memcmp(buf, "NeoBench installer disc", 23) == 0,
          "file bytes come back as staged");

    /* A path with a directory in it, spelled in the wrong case: the
     * name the disc forced into upper case must still match, and the
     * ";1" version must be invisible to the caller. */
    check(nb_iso_stat("docs/inner.txt", &lba, &size) == 1,
          "case-insensitive path through a directory");
    check(size == 11, "inner file is eleven bytes");
    {
        uint8_t b[16];

        memset(b, 0, sizeof b);
        check(nb_iso_read(lba, 0, b, 11) &&
              memcmp(b, "inner file\n", 11) == 0,
              "subdirectory file contents");
    }

    /* Unaligned read spanning a sector boundary of NBFS.IMG. */
    if (nb_iso_stat("NBFS.IMG", &lba, &size) && size > 4096)
    {
        uint8_t *big = malloc(3000);
        size_t off = (size_t)lba * CD_SEC + 100;

        if (big && off + 3000 <= iso_len)
        {
            check(nb_iso_read(lba, 100, big, 3000),
                  "unaligned spanning read completes");
            check(memcmp(big, iso_img + off, 3000) == 0,
                  "unaligned spanning read matches the disc");
        }
        else
            check(0, "spanning read set up");
        free(big);
    }
    else
        check(0, "NBFS.IMG present and larger than one block");

    check(!nb_iso_stat("NOPE.TXT", &lba, &size), "missing file is absent");
    check(!nb_iso_stat("DOCS", &lba, &size),
          "a directory is not a file to nb_iso_stat");
    check(!nb_iso_stat("", &lba, &size), "the empty path is not a file");
}

static void test_install(void)
{
    struct nb_install ins;
    uint32_t lba = 0, size = 0;

    check(nb_iso_stat("NBFS.IMG", &lba, &size) && size >= 4096 * 325u,
          "payload is a whole volume");
    check_u((unsigned)(size % 4096u), 0, "payload is block-aligned");

    /* No disk behind ata.device yet. */
    nb_install_run(&ins);
    check_u((unsigned)ins.state, NB_INSTALL_NO_DISK,
            "install with no disk says so");
    check(strstr(last_line, ">install state=no-disk") == last_line,
          "serial: >install state=no-disk");

    /* The RAM disk takes the payload. */
    disk_len = (size_t)size + 4096;
    disk = calloc(1, disk_len);
    if (!disk)
    {
        printf("FAIL: no memory for the RAM disk\n");
        failures++;
        return;
    }
    {
        struct nb_dev d;

        memset(&d, 0, sizeof d);
        d.name = "ata.device";
        d.units = 1;
        d.secsize = HD_SEC;
        d.read = hd_read;
        d.write = hd_write;
        check(nb_dev_add(&d) >= 0, "ata.device binds");
    }

    nb_install_run(&ins);
    check_u((unsigned)ins.state, NB_INSTALL_DONE, "install onto a blank disk");
    check(strstr(last_line, ">install state=done") == last_line,
          "serial: >install state=done");
    check(ins.sectors == (unsigned)((size + HD_SEC - 1) / HD_SEC),
          "reported sector count matches the image");
    check(strcmp(ins.volume, "NeoBench") == 0,
          "volume name read back from the written superblock");
    check(memcmp(disk, "NBBOOT", 6) == 0, "disk starts with NBBOOT");
    check(memcmp(disk + 4096, "NBFS", 4) == 0,
          "superblock sits at NBFS block 1");
    check(memcmp(disk + 4096 + 104, "NeoBench", 8) == 0,
          "superblock volume name");

    /* A second boot finds the volume it wrote. */
    nb_install_run(&ins);
    check_u((unsigned)ins.state, NB_INSTALL_PRESENT,
            "install finds its own volume");
    check(strcmp(ins.volume, "NeoBench") == 0,
            "present volume is named");
    check(strstr(last_line, ">install state=present") == last_line,
          "serial: >install state=present");

    /* Someone else's data: not blank, not ours. */
    disk[0] = 0x44;                       /* 'D' of an Amiga boot block */
    nb_install_run(&ins);
    check_u((unsigned)ins.state, NB_INSTALL_REFUSED,
            "a disk that holds data is left alone");
    check(strstr(last_line, ">install state=refused") == last_line,
          "serial: >install state=refused");

    /* Our boot block, someone else's superblock: damaged, not ours. */
    memcpy(disk, "NBBOOT", 6);
    disk[4096] = 'X';
    nb_install_run(&ins);
    check_u((unsigned)ins.state, NB_INSTALL_DAMAGED,
            "a damaged volume is not re-installed over");
}

int main(void)
{
    struct nb_dev d;

    iso_img = load("build/fixture.iso", &iso_len);
    if (!iso_img || iso_len < 17 * CD_SEC)
        return 1;

    memset(&d, 0, sizeof d);
    d.name = "atapi.device";
    d.units = 1;
    d.secsize = CD_SEC;
    d.read = cd_read;
    d.write = 0;
    check(nb_dev_add(&d) >= 0, "atapi.device binds");

    test_mount();
    test_lookup();
    test_install();

    free(iso_img);
    free(disk);

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_iso9660: all checks passed\n");
    return 0;
}
