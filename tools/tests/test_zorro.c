/*
 * Host test of the Zorro autoconfig decoder (boot/rom/zorro.c).
 *
 * The one thing that cannot be tested in the emulator is the encoding
 * itself, because FS-UAE has no ZZ9000 to put in the window -- so the
 * window is synthesised here from the other direction: the bytes are
 * written exactly the way the Amiga's autoconfig hardware takes a
 * configuration byte (nibbles apart, everything but er_Type and the
 * control byte at $40 inverted), and then read back with the code the
 * ROM will run.  If the two halves of that encoding ever disagree, this
 * fails rather than the boot quietly reading an inverted manufacturer.
 *
 *   cc -O2 -Wall -Wextra -o test_zorro test_zorro.c ../../boot/rom/zorro.c
 *   ./test_zorro
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../../boot/rom/zorro.h"
#include "../../boot/rom/zz9000.h"

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
        printf("FAIL: %s: got %u ($%x), want %u ($%x)\n",
               what, got, got, want, want);
    }
}

/*
 * Write one configuration byte the way the hardware stores it: its high
 * nibble at the byte's own address and its low nibble at +2, in the top
 * half of each, inverted -- except at $00 and $40, which the ROM leaves
 * alone.  `off' is the absolute offset in the window, so a board in the
 * second slot has its er_Type at $40 and is subject to that exception,
 * exactly as the hardware treats it.
 */
static void put(uint8_t *win, unsigned off, uint8_t v)
{
    if (off == 0 || off == 0x40)
    {
        win[off] = (uint8_t)(v & 0xf0);
        win[off + 2] = (uint8_t)((v & 0x0f) << 4);
    }
    else
    {
        win[off] = (uint8_t)~(v & 0xf0);
        win[off + 2] = (uint8_t)~((v & 0x0f) << 4);
    }
}

/* The sixteen logical bytes of one board's ExpansionRom, at slot `slot'. */
static void board(uint8_t *win, unsigned slot, uint8_t type, uint8_t product,
                  uint8_t flags, uint16_t manuf, uint32_t serial)
{
    unsigned b = slot * NB_ZSLOT;

    put(win, b + 0x00, type);
    put(win, b + 0x04, product);
    put(win, b + 0x08, flags);
    put(win, b + 0x0c, 0);                  /* er_Reserved              */
    put(win, b + 0x10, (uint8_t)(manuf >> 8));
    put(win, b + 0x14, (uint8_t)(manuf & 0xff));
    put(win, b + 0x18, (uint8_t)(serial >> 24));
    put(win, b + 0x1c, (uint8_t)(serial >> 16));
    put(win, b + 0x20, (uint8_t)(serial >> 8));
    put(win, b + 0x24, (uint8_t)(serial));
    /* bytes 10..15 are reserved: nothing writes them, and the window is
     * cleared to $FF first, which decodes to zero for an inverted byte */
}

static void test_decode(void)
{
    uint8_t win[512];
    unsigned i;

    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, 0x20, 0x6d6e, 0x12345678UL);

    /* Every byte has to come back as it went in, inverted ones and all. */
    check_u(nb_zorro_byte(win, 0x00), 0x83, "er_Type");
    check_u(nb_zorro_byte(win, 0x04), 4, "er_Product");
    check_u(nb_zorro_byte(win, 0x08), 0x20, "er_Flags");
    check_u(nb_zorro_byte(win, 0x0c), 0x00, "er_Reserved");
    check_u(nb_zorro_byte(win, 0x10), 0x6d, "er_Manufacturer high");
    check_u(nb_zorro_byte(win, 0x14), 0x6e, "er_Manufacturer low");
    check_u(nb_zorro_byte(win, 0x18), 0x12, "er_SerialNumber byte 0");
    check_u(nb_zorro_byte(win, 0x24), 0x78, "er_SerialNumber byte 3");

    /* The reserved half of the window still decodes: every inverted
     * byte of an all-$FF window comes out zero, which is why zero alone
     * is not proof of a board. */
    for (i = 0x28; i < 0x40; i += 4)
        check_u(nb_zorro_byte(win, i), 0, "reserved byte");
}

static void test_sizes(void)
{
    /* Zorro III with the extended size table: 16 MB shifted by the low
     * three bits of er_Type.  $83 -> 128 MB is the ZZ9000 Z3. */
    check_u(nb_zorro_size(0x83, NB_ZF_EXT_SIZE, 3), 128u * 1024u * 1024u,
            "Z3 $83 + ext_size");
    check_u(nb_zorro_size(0x84, NB_ZF_EXT_SIZE, 3), 256u * 1024u * 1024u,
            "Z3 $84 + ext_size (256 MB card)");
    check_u(nb_zorro_size(0x80, 0, 3), 16u * 1024u * 1024u,
            "Z3 without ext_size");
    check_u(nb_zorro_size(0x80, NB_ZF_EXT_SIZE, 3), 16u * 1024u * 1024u,
            "Z3 ext_size, code zero");

    /* Zorro II's own ladder: $C7 -> 4 MB, which is the ZZ9000 Z2, and
     * code zero is the largest board on that bus. */
    check_u(nb_zorro_size(0xc7, 0, 2), 4u * 1024u * 1024u, "Z2 $C7");
    check_u(nb_zorro_size(0xc0, 0, 2), 8u * 1024u * 1024u, "Z2 $C0");
    check_u(nb_zorro_size(0xc1, 0, 2), 64u * 1024u, "Z2 $C1");

    check_u(nb_zorro_family(0x83), 3, "family of $83");
    check_u(nb_zorro_family(0xc7), 2, "family of $C7");
    check_u(nb_zorro_family(0x40), 1, "family of $40");
    check_u(nb_zorro_family(0x00), 0, "family of nothing");
    check_u(nb_zorro_family(0x3f), 0, "family of $3f");
}

static void test_scan_zz9000(void)
{
    uint8_t win[512];
    struct nb_zdev d[4];
    unsigned n;

    /* The Zorro III card: MNT, product 4, extended size. */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, 0x20, NB_ZZ9000_MANUF, 0xdeadbeefUL);
    n = nb_zorro_scan(win, 16, d, 4);
    check_u(n, 1, "one board in the window");
    check_u(d[0].manuf, NB_ZZ9000_MANUF, "manufacturer");
    check_u(d[0].product, 4, "product");
    check_u(d[0].zorro, 3, "zorro generation");
    check_u(d[0].serial, 0xdeadbeefUL, "serial");
    check_u(d[0].size, 128u * 1024u * 1024u, "window size");

    /* The Zorro II card, and the 256 MB revision. */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0xc7, 3, 0, NB_ZZ9000_MANUF, 1);
    n = nb_zorro_scan(win, 16, d, 4);
    check_u(n, 1, "Z2 card found");
    check_u(d[0].zorro, 2, "Z2 generation");
    check_u(d[0].size, 4u * 1024u * 1024u, "Z2 window size");

    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x84, 5, NB_ZF_EXT_SIZE, NB_ZZ9000_MANUF, 2);
    n = nb_zorro_scan(win, 16, d, 4);
    check_u(n, 1, "256 MB card found");
    check_u(d[0].size, 256u * 1024u * 1024u, "256 MB window size");
}

static void test_scan_two_boards(void)
{
    uint8_t win[512];
    struct nb_zdev d[4];
    unsigned n;

    /*
     * A board in each slot.  The second board's er_Type sits at offset
     * $40, the one address in the window the ROM does not invert, so
     * this is the case that catches an encoder and a decoder that agree
     * about everything except where that exception applies.
     */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0xc1, 7, 0, 0x0b3e, 0x01020304UL);
    board(win, 1, 0x83, 4, NB_ZF_EXT_SIZE, NB_ZZ9000_MANUF, 5);
    n = nb_zorro_scan(win, 16, d, 4);
    check_u(n, 2, "two boards, second at $40");
    check_u(d[0].manuf, 0x0b3e, "first board manufacturer");
    check_u(d[0].size, 64u * 1024u, "first board size");
    check_u(d[1].manuf, NB_ZZ9000_MANUF, "second board manufacturer");
    check_u(d[1].product, 4, "second board product");
    check_u(d[1].zorro, 3, "second board generation");
}

static void test_empty_windows(void)
{
    uint8_t win[512];
    struct nb_zdev d[4];

    /*
     * A window nobody drives reads $FF.  er_Type then reads $FF -- a
     * valid-looking Zorro II type asking for the largest board there
     * is -- so er_Type alone would report a card out of thin air.  The
     * manufacturer is what stops it: it is one of the inverted bytes,
     * so $FF decodes to $0000 and the walk ends with nothing found.
     */
    memset(win, 0xff, sizeof(win));
    check_u(nb_zorro_scan(win, 16, d, 4), 0, "an open window is empty");
    check_u(nb_zorro_byte(win, 0), 0xff, "open window er_Type");

    memset(win, 0x00, sizeof(win));
    check_u(nb_zorro_scan(win, 16, d, 4), 0, "a pulled-down window");

    /*
     * The same, one slot on: FS-UAE leaves the control byte at $40 at
     * zero once the chain has been walked, and the window behind it at
     * $FF.  The first board must still be reported and the walk must
     * stop there rather than run into the noise.
     */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, NB_ZF_EXT_SIZE, NB_ZZ9000_MANUF, 9);
    check_u(nb_zorro_scan(win, 16, d, 4), 1, "board, then the end");
    win[0x40] = 0;
    win[0x42] = 0;
    check_u(nb_zorro_scan(win, 16, d, 4), 1, "board, then a zero $40");
}

static void test_structural(void)
{
    uint8_t win[512];
    struct nb_zdev d[4];

    /* A reserved byte that is not zero: noise, and the end of the
     * chain -- not a board to be skipped over. */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, NB_ZF_EXT_SIZE, NB_ZZ9000_MANUF, 0);
    put(win, 0x0c, 0x5a);
    check_u(nb_zorro_scan(win, 16, d, 4), 0, "reserved byte must be zero");

    /* A manufacturer of zero is nobody's card. */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, NB_ZF_EXT_SIZE, 0, 0);
    check_u(nb_zorro_scan(win, 16, d, 4), 0, "manufacturer must be somebody");

    /* And one that fails neither test but is not the MNT card: it has
     * to be found by the scan and then not matched, which is the whole
     * reason the identity is checked rather than assumed. */
    memset(win, 0xff, sizeof(win));
    board(win, 0, 0x83, 4, NB_ZF_EXT_SIZE, 0x02fc, 0);
    check_u(nb_zorro_scan(win, 16, d, 4), 1, "some other board");
    check(d[0].manuf != NB_ZZ9000_MANUF, "not the MNT card");
}

int main(void)
{
    test_decode();
    test_sizes();
    test_scan_zz9000();
    test_scan_two_boards();
    test_empty_windows();
    test_structural();

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_zorro: all checks passed\n");
    return 0;
}
