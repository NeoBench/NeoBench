#ifndef NB_DEV_H
#define NB_DEV_H

#include <stdint.h>

/*
 * NeoBench's own device layer -- the `.device`s this system starts itself.
 *
 * A device here is one named driver with units behind it: ata.device owns
 * the fixed disks, atapi.device the CD-ROM, sdcard.device the removable
 * socket, and so on.  Nothing is looked up in a table of what some other
 * system had loaded: a device is in this table because NeoBench talked to
 * the hardware it names and the hardware answered, and every entry is
 * built from that answer rather than from an expectation.
 *
 * Units are numbered per device from 0, while `base' records the unit
 * number the device's unit 0 carries on the controller it rides -- the
 * same physical bus can hold a disk on unit 0 and a card on unit 1, and
 * each of them still wants its own unit 0.
 *
 * Transfers are by the device's own sectors: 512 bytes on a disk, 2048
 * on a CD-ROM, reported as `secsize' so a caller never has to guess.
 * A driver that has no sector path -- a windowed card, a sound channel --
 * says so by leaving read and write null, which is a different answer
 * from a driver whose hardware did not answer, and the table keeps them
 * apart.
 */

struct nb_dev
{
    const char *name;      /* "ata.device": what it answers to              */
    unsigned    units;     /* unit 0 .. units-1                             */
    unsigned    base;      /* unit 0's number on the controller             */
    unsigned    secsize;   /* bytes per sector; 0 when it is not a disk     */
    int (*read)(unsigned unit, uint32_t lba, void *dst, unsigned count);
    int (*write)(unsigned unit, uint32_t lba, const void *src, unsigned count);
};

#define NB_DEV_MAX 16

/*
 * Bind a device.  The entry is copied, so the caller may build it on the
 * stack; a name already in the table is refused, because two entries for
 * one device would leave a reader unable to say which of them answered.
 * Returns the index, or -1 when the table is full or the name is taken.
 */
int nb_dev_add(const struct nb_dev *d);

/* Look a device up by name, or list the table: 0 and count when absent. */
const struct nb_dev *nb_dev_find(const char *name);
unsigned nb_dev_count(void);
const struct nb_dev *nb_dev_at(unsigned i);

/*
 * Sector I/O through the device called `name', unit `unit'.  The unit is
 * the caller's own numbering: 0 means the device's first unit, however
 * many the controller has before it.  Returns 1 when every sector moved,
 * 0 otherwise -- no device, no such unit, no sector path, or hardware
 * that refused the command.
 */
int nb_dev_read(const char *name, unsigned unit, uint32_t lba, void *dst,
                unsigned count);
int nb_dev_write(const char *name, unsigned unit, uint32_t lba,
                 const void *src, unsigned count);

/* One serial line per device: what is bound, and what it can do. */
void nb_dev_dump(void);

#endif /* NB_DEV_H */
