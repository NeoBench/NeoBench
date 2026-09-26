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
};

/*
 * Probe one Gayle IDE unit: 0 = master, 1 = slave.
 *
 * Returns non-zero when the unit completed IDENTIFY, with *id filled in.
 * Never blocks for long and never blocks forever: a missing, slow or wedged
 * drive answers "no", it does not stall the boot.
 */
int nb_ata_identify(unsigned unit, struct nb_ata_id *id);

#endif /* NB_ATA_H */
