#ifndef NB_ZZ9000_H
#define NB_ZZ9000_H

#include <stdint.h>

/*
 * zz9000.device: MNT Research's ZZ9000, and the ZZ9000AX module that
 * clips to it.
 *
 * The card is one Zorro configuration function carrying everything --
 * RTG output, Ethernet and the AX audio interface all sit behind the
 * same register file -- so there is one thing to find and one driver to
 * start.  The AX is not a second board: it is a daughterboard the card
 * reports through a register, which is why it can only be asked about
 * once the card has an address.
 *
 * Products are the ones MNT registered: 3 is the Zorro II card, 4 the
 * Zorro III card, 5 the 256 MB Zorro III revision.
 */

#define NB_ZZ9000_MANUF     0x6d6eu  /* $6D6E, "mn" -- MNT Research   */
#define NB_ZZ9000_PRODUCT_Z2    3u
#define NB_ZZ9000_PRODUCT_Z3    4u
#define NB_ZZ9000_PRODUCT_Z3_256 5u

struct nb_zz9000
{
    int      present;   /* it parses in the autoconfig window        */
    int      bound;     /* given an address, answering, driver on it  */
    int      ax;        /* the ZZ9000AX module is fitted             */
    uint8_t  product;   /* 3, 4 or 5                                 */
    uint8_t  zorro;     /* 2 or 3                                    */
    uint32_t size;      /* the window it asked for, in bytes         */
    uint32_t base;      /* where it was put; 0 when it was not       */
    uint16_t fw;        /* firmware version word; 0 when it did not
                         * answer                                     */
};

/*
 * Look for the card, put it on the bus if it is there, and report what
 * came of it.  Returns a pointer to static state: one call per boot,
 * and repeated calls return the same answer without touching hardware
 * again.
 */
const struct nb_zz9000 *nb_zz9000_probe(void);

#endif /* NB_ZZ9000_H */
