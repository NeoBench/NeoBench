#ifndef NB_ZORRO_H
#define NB_ZORRO_H

#include <stdint.h>

/*
 * The Zorro autoconfig ROM, read but never written.
 *
 * Every board on the bus describes itself from a small window that the
 * machine decodes for exactly this purpose: $E80000 for Zorro I and II
 * boards, $FF000000 for Zorro III, sixty-four bytes to a board.  The
 * window is the one place above $D80000 this system reads without
 * knowing what is there first, and it is safe for the two reasons that
 * make any probe here honest -- a read has no side effect on a config
 * ROM, and the bytes have to parse as a config ROM before any of them
 * is believed (zorro.c).
 *
 * A board is one struct.  Nothing here assigns an address, moves a card
 * or writes a byte: `nb_zorro_scan' answers what is in the window and
 * no more, which is the whole difference between a scan and a rumour.
 */

/* Bytes of configuration space one board occupies. */
#define NB_ZSLOT            64u

/* er_Flags: take the window size from the extended Zorro III table
 * instead of the default one (wiki.amigaos.net, Expansion Library). */
#define NB_ZF_EXT_SIZE      0x20u

/* One board as its own ROM describes it. */
struct nb_zdev
{
    uint8_t  type;      /* er_Type                                 */
    uint8_t  product;   /* er_Product                              */
    uint8_t  flags;     /* er_Flags                                */
    uint16_t manuf;     /* er_Manufacturer -- who built it         */
    uint32_t serial;    /* er_SerialNumber                         */
    uint32_t size;      /* the window the board asks for, in bytes */
    uint8_t  zorro;     /* 1, 2 or 3; 0 when er_Type selects none  */
};

/*
 * The family er_Type's top two bits select -- $40 prototype, $C0
 * Zorro II, $80 Zorro III -- or 0 when they select nothing, which is
 * what an empty window reads back as.
 */
uint8_t nb_zorro_family(uint8_t type);

/*
 * One configuration byte, decoded, at byte offset `off' from the start
 * of the window `rom'.  The ROM hands out four addresses per byte and
 * inverts all but two of them; see zorro.c.
 */
uint8_t nb_zorro_byte(const volatile uint8_t *rom, uint32_t off);

/* The window er_Type and er_Flags ask for, in bytes. */
uint32_t nb_zorro_size(uint8_t type, uint8_t flags, uint8_t zorro);

/*
 * Walk `nslots' boards' worth of window at `rom' and collect the boards
 * that are there, in order, up to `max'.  The walk stops at the first
 * slot that does not parse as a board: the chain is linked in order, so
 * a slot that is empty ends it, and so does one that is noise.
 */
unsigned nb_zorro_scan(const volatile uint8_t *rom, unsigned nslots,
                       struct nb_zdev *out, unsigned max);

#endif /* NB_ZORRO_H */
