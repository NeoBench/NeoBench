/*
 * NeoBench's device table.
 *
 * The table lives in .bss on purpose: it is writable state, and this link
 * puts .data in the ROM at $FC0000 where hardware drops the write, so a
 * table with an explicit zero in it would never accept a single entry.
 * Uninitialised statics land in .bss in chip RAM instead, which is where
 * everything this system owns at run time has to live.
 */
#include <stdint.h>
#include "dev.h"
#include "amiga.h"

static struct nb_dev devs[NB_DEV_MAX];
static unsigned ndevs;

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

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

int nb_dev_add(const struct nb_dev *d)
{
    if (!d || !d->name || !d->name[0] || ndevs >= NB_DEV_MAX)
        return -1;
    if (nb_dev_find(d->name))
        return -1;
    devs[ndevs] = *d;
    return (int)ndevs++;
}

const struct nb_dev *nb_dev_find(const char *name)
{
    unsigned i;

    if (!name || !name[0])
        return 0;
    for (i = 0; i < ndevs; i++)
        if (same(devs[i].name, name))
            return &devs[i];
    return 0;
}

unsigned nb_dev_count(void)
{
    return ndevs;
}

const struct nb_dev *nb_dev_at(unsigned i)
{
    return i < ndevs ? &devs[i] : 0;
}

int nb_dev_read(const char *name, unsigned unit, uint32_t lba, void *dst,
                unsigned count)
{
    const struct nb_dev *d = nb_dev_find(name);

    if (!d || !d->read || !dst || !count || unit >= d->units)
        return 0;
    return d->read(d->base + unit, lba, dst, count);
}

int nb_dev_write(const char *name, unsigned unit, uint32_t lba,
                 const void *src, unsigned count)
{
    const struct nb_dev *d = nb_dev_find(name);

    if (!d || !d->write || !src || !count || unit >= d->units)
        return 0;
    return d->write(d->base + unit, lba, src, count);
}

void nb_dev_dump(void)
{
    unsigned i;

    s_put(">dev n=");
    s_u(ndevs);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');

    for (i = 0; i < ndevs; i++)
    {
        const struct nb_dev *d = &devs[i];

        s_put(">dev ");
        s_put(d->name);
        s_put(" u=");
        s_u(d->units);
        s_put(" b=");
        s_u(d->base);
        s_put(" s=");
        s_u(d->secsize);
        s_put(" io=");
        if (d->read && d->write)
            s_put("read,write");
        else if (d->read)
            s_put("read");
        else if (d->write)
            s_put("write");
        else
            s_put("none");
        amiga_serial_putc('\r');
        amiga_serial_putc('\n');
    }
}
