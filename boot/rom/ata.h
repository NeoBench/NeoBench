#ifndef NB_ATA_H
#define NB_ATA_H

/*
 * One answered IDE unit, filled from ATA / ATAPI IDENTIFY.
 */
struct nb_ata_id
{
    char     model[48];   /* IDENTIFY words 27..46, printable, NUL ended */
    unsigned mb;          /* ATA capacity in MiB; 0 for an ATAPI device   */
    int      atapi;       /* the unit answered IDENTIFY PACKET DEVICE    */
    int      present;     /* the unit answered at all                    */
    unsigned status;      /* task file status as the probe left it        */
    unsigned ident;       /* IDENTIFY word 0: the device magic, $FFFF none */
    unsigned sig;         /* cylinder low/high as the unit left them on
                            * selection: $14/$EB is a packet device, and
                            * the one mark a drive gives even when it
                            * will offer no IDENTIFY at all              */
};

/*
 * Probe one Gayle IDE unit: 0 = master, 1 = slave.
 *
 * Returns non-zero when the unit completed IDENTIFY, with *id filled in.
 * Never blocks for long and never blocks forever: a missing, slow or wedged
 * drive answers "no", it does not stall the boot.
 */
int nb_ata_identify(unsigned unit, struct nb_ata_id *id);

/*
 * Sector I/O on a unit: 512 byte sectors from ata.device, 2048 byte
 * sectors from atapi.device, `lba' counted in that device's own sectors.
 *
 * Returns 1 when every sector moved and the device finished clean, 0
 * otherwise -- no unit, no such address, a write to a read-only medium,
 * or hardware that refused the command.  These are the entry points the
 * device table binds under "ata.device" and "atapi.device".
 */
int nb_ata_read(unsigned unit, uint32_t lba, void *dst, unsigned count);
int nb_ata_write(unsigned unit, uint32_t lba, const void *src,
                 unsigned count);
int nb_atapi_read(unsigned unit, uint32_t lba, void *dst, unsigned count);

/* The task file status as the last command on the bus left it. */
unsigned nb_ata_status(void);

#endif /* NB_ATA_H */
