/*
 * Reading the Zorro autoconfig ROM.
 *
 * The encoding, because nothing about it is guessable from the shape of
 * the structure: the window holds four addresses to the byte.  Logical
 * byte i lives at address 4i (its high nibble) and 4i+2 (its low nibble,
 * still up in the top half), and every byte but two arrives inverted --
 * the ROM's own assembly writes (~v)&$f000 for each one.  So the sixteen
 * bytes of a board's ExpansionRom occupy 0x00, 0x04 ... 0x3C, and the
 * window is the sixty-four bytes that leaves.  er_Type at offset 0 and
 * the control byte at offset $40 are the two the ROM does not invert;
 * that is stated in the AmigaOS expansion documentation ("nibbles $40
 * and $42 are not to be inverted") and it is what WinUAE's expamem_read
 * does, byte for byte, so both halves of the encoding cross-check.
 *
 * Decoding a floating bus therefore has to be possible and has to be
 * caught.  A window nobody drives reads $FF: er_Type comes out $FF,
 * which is a valid-looking Zorro II type with the largest size nibble
 * there is, so er_Type alone cannot tell a board from nothing.  The
 * other two fields can -- a board states who built it and leaves the
 * reserved byte at zero, while an all-$FF window decodes the reserved
 * byte to zero *and* the manufacturer to $0000, because that byte is one
 * of the inverted ones.  Hence the rule in nb_zorro_scan: a family, a
 * zero reserved byte, and a manufacturer that is somebody.  A card that
 * fails any of the three ends the walk rather than being skipped, since
 * the chain is a linked list and a slot that does not parse means there
 * is nothing after it either.
 */
#include <stdint.h>
#include "zorro.h"

uint8_t nb_zorro_family(uint8_t type)
{
    switch (type & 0xc0u)
    {
    case 0x40u: return 1;               /* prototype / Zorro I         */
    case 0xc0u: return 2;               /* Zorro II                    */
    case 0x80u: return 3;               /* Zorro III                   */
    default:    return 0;
    }
}

uint8_t nb_zorro_byte(const volatile uint8_t *rom, uint32_t off)
{
    uint8_t hi = rom[off];
    uint8_t lo = rom[off + 2];
    uint8_t b = (uint8_t)((hi & 0xf0u) | (lo >> 4));

    if (off == 0 || off == 0x40)
        return b;
    return (uint8_t)~b;
}

uint32_t nb_zorro_size(uint8_t type, uint8_t flags, uint8_t zorro)
{
    unsigned code = (unsigned)(type & 7u);

    /*
     * Zorro III takes the size from the extended table when er_Flags
     * says so: sixteen megabytes shifted by the low three bits of
     * er_Type, which is where 128 MB comes from for type $83.  Zorro II
     * has its own ladder of 32 KB shifts, with zero meaning the largest
     * board on that bus.  Both are the same arithmetic the OS does when
     * it decides how much address space to hand a card.
     */
    if (zorro == 3)
        return (flags & NB_ZF_EXT_SIZE) ? ((uint32_t)16777216ul << code)
                                        : (uint32_t)16777216ul;
    if (code == 0)
        return (uint32_t)8388608ul;
    return (uint32_t)32768ul << code;
}

unsigned nb_zorro_scan(const volatile uint8_t *rom, unsigned nslots,
                       struct nb_zdev *out, unsigned max)
{
    unsigned n = 0, s;

    for (s = 0; s < nslots && n < max; s++)
    {
        uint32_t base = (uint32_t)s * NB_ZSLOT;
        struct nb_zdev d;
        uint8_t reserved;

        d.type = nb_zorro_byte(rom, base + 0x00);
        d.zorro = nb_zorro_family(d.type);
        if (!d.zorro)
            break;                      /* the chain ends here          */

        reserved = nb_zorro_byte(rom, base + 0x0c);
        d.manuf = (uint16_t)(((unsigned)nb_zorro_byte(rom, base + 0x10) << 8) |
                              nb_zorro_byte(rom, base + 0x14));
        if (reserved != 0 || d.manuf == 0)
            break;                      /* noise, not a board           */

        d.product = nb_zorro_byte(rom, base + 0x04);
        d.flags = nb_zorro_byte(rom, base + 0x08);
        d.serial = ((uint32_t)nb_zorro_byte(rom, base + 0x18) << 24) |
                   ((uint32_t)nb_zorro_byte(rom, base + 0x1c) << 16) |
                   ((uint32_t)nb_zorro_byte(rom, base + 0x20) << 8) |
                   ((uint32_t)nb_zorro_byte(rom, base + 0x24));
        d.size = nb_zorro_size(d.type, d.flags, d.zorro);
        out[n++] = d;
    }
    return n;
}
