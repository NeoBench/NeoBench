#ifndef NB_SCSI_H
#define NB_SCSI_H

#include <stdint.h>

/*
 * What one NCR53C710 host said when it was asked: that it is there,
 * where in the board window it decodes, and which targets answered it.
 *
 * Nothing here is assumed from the machine's name.  `found' is the chip
 * identifying itself, `units' is targets that ran a command to
 * completion, and `base' is the lowest of them -- the device table maps
 * scsi.device unit 0 onto `base', so the count and the starting target
 * are two separate facts about one bus.
 */
struct nb_scsi_info
{
    int      found;     /* the chip identified itself in the window      */
    unsigned win;       /* the window it answered in: $40 or $80         */
    unsigned base;      /* the lowest target that answered a command     */
    unsigned units;     /* how many targets from `base' answered         */
    unsigned mb;        /* capacity of `base', in MiB                    */
    char     model[25]; /* INQUIRY product of `base', trimmed, NUL ended */
};

/*
 * Look for the host once and keep the answer.  Bounded like every other
 * probe in this system: a target that is not on the bus is found by its
 * silence, and silence is waited for no longer than a drive that is
 * still spinning up could plausibly need.
 */
const struct nb_scsi_info *nb_scsi_probe(void);

/*
 * Sector I/O in 512 byte sectors, `unit' the target as the host numbers
 * it -- the same number the device table added to `base' before asking.
 *
 * Returns 1 when every sector moved and the target finished with GOOD,
 * 0 otherwise: no such target, an address past the end of the disk (a
 * question this driver answers from its own capacity rather than by
 * letting the bus refuse it), or a command the bus would not complete.
 */
int nb_scsi_read(unsigned unit, uint32_t lba, void *dst, unsigned count);
int nb_scsi_write(unsigned unit, uint32_t lba, const void *src,
                  unsigned count);

/* The SCSI status byte the last command left: 0 is GOOD. */
unsigned nb_scsi_status(void);

#endif /* NB_SCSI_H */
