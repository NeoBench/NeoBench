/*
 * The installer (install.h).
 *
 * One file on the disc, one run at boot, and a rule that would rather
 * do nothing than overwrite anything: the disk's first sector decides
 * between blank, ours and someone else's before a single byte is
 * written.  The stream itself is deliberately dumb -- a sector from
 * the CD, four sectors to the disk -- because correctness here is a
 * property of the image arriving whole, not of anything clever about
 * the copy.
 *
 * The disk is reached as "ata.device" unit 0 through the device
 * table, never through Gayle's registers: by the time this runs the
 * table already says whether a disk answered, and a host test can put
 * a RAM disk behind the same name (tools/tests/test_iso9660.c).
 */
#include <stdint.h>

#include "amiga.h"
#include "dev.h"
#include "install.h"
#include "iso9660.h"

#define SECTOR 2048u          /* one CD sector, as atapi counts them */
#define DISK_SEC 512u         /* one disk sector, as ata counts them */
#define SUPER_LBA 8u          /* NBFS block 1: 4096 / 512 disk sectors */

static uint8_t sect[SECTOR];  /* .bss: the stream window             */

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

static const char *state_name(enum nb_install_state s)
{
    switch (s)
    {
    case NB_INSTALL_NO_PAYLOAD: return "no-payload";
    case NB_INSTALL_NO_DISK:    return "no-disk";
    case NB_INSTALL_REFUSED:    return "refused";
    case NB_INSTALL_DAMAGED:    return "damaged";
    case NB_INSTALL_PRESENT:    return "present";
    case NB_INSTALL_DONE:       return "done";
    default:                    return "failed";
    }
}

static int starts(const uint8_t *p, const char *sig)
{
    while (*sig)
        if (*p++ != (uint8_t)*sig++)
            return 0;
    return 1;
}

/* The volume name lives in the superblock at byte 104 (NBFS V1). */
static void read_volume_name(struct nb_install *r)
{
    const uint8_t *v = sect + 104;
    unsigned i;

    for (i = 0; i < 15 && v[i] && v[i] != ' '; i++)
        r->volume[i] = (char)v[i];
    r->volume[i] = '\0';
}

static void s_x2(uint8_t v)
{
    static const char hex[] = "0123456789abcdef";
    char b[3];

    b[0] = hex[(v >> 4) & 0xf];
    b[1] = hex[v & 0xf];
    b[2] = '\0';
    s_put(b);
}

void nb_install_run(struct nb_install *r)
{
    uint32_t lba = 0, size = 0, pos;
    unsigned i, secs;
    const struct nb_dev *d;
    /*
     * Read-back accounting: every chunk that went down is read again
     * and compared with what left the buffer.  ">install ... mis=" then
     * says which end of the copy mislaid the bytes if anything ever
     * differs -- the buffer leaving wrong (a disc-side fault) or the
     * platter landing wrong (a write-side fault) are two different
     * repairs, and a final image that merely does not match cannot tell
     * them apart on its own.  The first mismatch names its position and
     * the byte pair, which is usually the whole story.
     */
    uint8_t back[SECTOR];
    uint32_t checked = 0, mis = 0, misrd = 0, mis_pos = 0;
    uint32_t cdmis = 0, cd2rd = 0, cdmis_pos = 0;
    unsigned mis_k = 0, cdmis_k = 0;
    uint8_t mis_e = 0, mis_a = 0, cdmis_e = 0, cdmis_a = 0;

    r->state = NB_INSTALL_NO_PAYLOAD;
    r->sectors = 0;
    r->volume[0] = '\0';
    r->op = 0;
    r->lba = 0;

    if (!nb_iso_stat("NBFS.IMG", &lba, &size) || size == 0)
        goto out;

    d = nb_dev_find("ata.device");
    if (!d || !d->units || !d->read || !d->write)
    {
        r->state = NB_INSTALL_NO_DISK;
        goto out;
    }

    if (!nb_dev_read("ata.device", 0, 0, sect, 1))
    {
        r->op = "hd";
        r->lba = 0;
        r->state = NB_INSTALL_FAILED;
        goto out;
    }

    if (starts(sect, "NBBOOT"))
    {
        /*
         * Our own boot block: the disk is either an installed volume
         * or the remains of one.  Both are left exactly as they are --
         * re-installing over a damaged volume is the user's decision,
         * not the boot's.
         */
        if (!nb_dev_read("ata.device", 0, SUPER_LBA, sect, 1) ||
            !starts(sect, "NBFS"))
        {
            r->state = NB_INSTALL_DAMAGED;
            goto out;
        }
        read_volume_name(r);
        r->state = NB_INSTALL_PRESENT;
        r->sectors = 0;
        goto out;
    }

    for (i = 0; i < DISK_SEC; i++)
        if (sect[i])
        {
            r->state = NB_INSTALL_REFUSED;
            goto out;
        }

    /*
     * Blank disk, payload in hand: stream it over.  Every chunk gets
     * three attempts -- a wait that times out once is a device that
     * was slow, three of them together are a device that will not
     * answer -- and the attempt that ends the run is recorded as it
     * happens: which side of the copy refused (disc read or disk
     * write) and at which sector, because a failure with no sector
     * named would ask the reader to guess.
     */
    for (pos = 0; pos < size; pos += SECTOR)
    {
        unsigned chunk = size - pos;
        unsigned tries;
        int ok = 0;

        if (chunk > SECTOR)
            chunk = SECTOR;
        for (i = chunk; i < SECTOR; i++)
            sect[i] = 0;            /* the tail lands as zeros        */
        for (tries = 0; tries < 3 && !ok; tries++)
        {
            r->op = "cd";
            r->lba = lba + pos / SECTOR;
            if (nb_iso_read(lba, pos, sect, chunk))
            {
                r->op = "hd";
                r->lba = pos / DISK_SEC;
                ok = nb_dev_write("ata.device", 0, pos / DISK_SEC, sect,
                                  SECTOR / DISK_SEC);
            }
            /* A disc read that refused is read again next pass, the
             * same as a disk write: the driver recovers a wedged unit
             * underneath us and the second attempt finds it. */
        }
        if (!ok)
        {
            r->sectors = (unsigned)(pos / DISK_SEC);
            r->state = NB_INSTALL_FAILED;
            goto out;
        }

        /* One read-back per chunk; see the accounting note above. */
        checked++;
        /*
         * A second reading of the same disc sector, compared with the
         * first before anything is written.  Two readings that agree
         * and a read-back that matches say the copy was right; two
         * that disagree say the delivery itself is at fault, and then
         * it does not matter that the platter faithfully holds one of
         * them.  The first disagreement names its position.
         */
        if (!nb_iso_read(lba, pos, back, chunk))
            cd2rd++;
        else
            for (i = 0; i < chunk; i++)
                if (back[i] != sect[i])
                {
                    if (!cdmis)
                    {
                        cdmis_pos = pos;
                        cdmis_k = i;
                        cdmis_e = sect[i];
                        cdmis_a = back[i];
                    }
                    cdmis++;
                    break;          /* count the chunk once          */
                }
        if (!nb_dev_read("ata.device", 0, pos / DISK_SEC, back,
                         SECTOR / DISK_SEC))
            misrd++;
        else
            for (i = 0; i < SECTOR; i++)
                if (back[i] != sect[i])
                {
                    if (!mis)
                    {
                        mis_pos = pos;
                        mis_k = i;
                        mis_e = sect[i];
                        mis_a = back[i];
                    }
                    mis++;
                    break;          /* count the chunk once          */
                }
    }

    secs = (size + DISK_SEC - 1) / DISK_SEC;
    if (!nb_dev_read("ata.device", 0, SUPER_LBA, sect, 1) ||
        !starts(sect, "NBFS"))
    {
        r->op = "verify";
        r->lba = SUPER_LBA;
        r->sectors = secs;          /* written, but it did not verify */
        r->state = NB_INSTALL_FAILED;
        goto out;
    }
    read_volume_name(r);
    r->sectors = secs;
    r->state = NB_INSTALL_DONE;

out:
    s_put(">install state=");
    s_put(state_name(r->state));
    s_put(" secs=");
    s_u(r->sectors);
    if (r->state == NB_INSTALL_FAILED && r->op)
    {
        s_put(" op=");
        s_put(r->op);
        s_put(" lba=");
        s_u((unsigned)r->lba);
    }
    if (r->volume[0])
    {
        s_put(" vol=");
        s_put(r->volume);
    }
    if (checked)
    {
        s_put(" chk=");
        s_u((unsigned)checked);
        s_put(" mis=");
        s_u((unsigned)mis);
        s_put(" misrd=");
        s_u((unsigned)misrd);
        s_put(" cdmis=");
        s_u((unsigned)cdmis);
        s_put(" cd2rd=");
        s_u((unsigned)cd2rd);
    }
    if (mis)
    {
        amiga_serial_putc('\r');
        amiga_serial_putc('\n');
        s_put(">install mis pos=");
        s_u((unsigned)mis_pos);
        s_put(" k=");
        s_u(mis_k);
        s_put(" e=");
        s_x2(mis_e);
        s_put(" a=");
        s_x2(mis_a);
    }
    if (cdmis)
    {
        amiga_serial_putc('\r');
        amiga_serial_putc('\n');
        s_put(">install cdmis pos=");
        s_u((unsigned)cdmis_pos);
        s_put(" k=");
        s_u(cdmis_k);
        s_put(" e=");
        s_x2(cdmis_e);
        s_put(" a=");
        s_x2(cdmis_a);
    }
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}
