/*
 * Host test of the Vista chrome's paint: the Rust half held against
 * the C.
 *
 * Phase 3 of the kernel-to-DE migration moved the scene the boot
 * leaves on screen -- the wash and its aurora, the glass bar, the
 * orb -- into libs/nb_rs/src/gfx.rs, and kernel_init's rs_gfx()
 * battery is the C standing over it: every formula and every table
 * below is written out from the numbers wallpaper(), aero_field()
 * and start_orb() carried, and the boot calls nb_rs_gfx_check()
 * with the Config's own glass and backdrop so each leg stands on
 * values nothing knew when this compiled.  This test brings the
 * same reference to the host, where it can sweep what a boot only
 * ever tries once: glasses across the whole Config range, and
 * backdrops from a night blue to a cream and over the exact weight
 * bd_dark() turns on at (598 is dark, 600 is not -- there is no
 * odd weight, the sum is always even).
 *
 * Agreement alone would pass two halves that were both wrong, so
 * after it every family is falsified on its own -- one corrupted
 * word, one flipped seed, one wrong length at a time -- and the
 * mask that comes back has to be exactly that family's bit with
 * `at` naming the word that moved, in the order the check walks
 * them.  A check that cannot fail is not a check.
 *
 *   cc -O2 -Wall -Wextra -std=c99 -o test_gfx test_gfx.c \
 *       ../../libs/nb_rs/target/release/libnb_rs.a -lpthread -ldl -lm
 *   ./test_gfx
 */
#include <stdint.h>
#include <stdio.h>

#include "../../boot/rom/gfx.h"

/* The Rust half, as kernel_init declares it. */
extern unsigned nb_rs_gfx_check(unsigned glass,
                                unsigned top, unsigned bot,
                                unsigned dark_top, unsigned dark_bot,
                                const unsigned *aero, unsigned naero,
                                const unsigned *bloom, unsigned nbloom,
                                const unsigned *pal, unsigned npal,
                                const unsigned *layer, unsigned nlayer,
                                const unsigned *orb, unsigned norb,
                                unsigned *at);

/* The mask's bits, as gfx.rs sets them, and the answer a bad call
 * comes back with -- the same 0x0bad_000N family `at` carries. */
#define F_DARK   0x00000001u
#define F_AERO   0x00000002u
#define F_LAYER  0x00000004u
#define F_BLOOM  0x00000008u
#define F_PAL    0x00000010u
#define F_ORB    0x00000020u
#define F_BAD    0x80000000u
#define BAD_CALL 0x80000000u

/* The lengths the check takes as itself; anything else is F_BAD
 * with the length's own 0x0bad_000N in `at`. */
#define AERO_N      22u
#define PAINT_WORDS 28u
#define PAL_N       23u
#define LAYER_N     5u
#define ORB_N       17u
#define ORB_WORDS   (ORB_N * 2u)

#define SENTINEL 0x5eed5eedu

static int failures;

static void ok(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void expect(const char *what, unsigned mask, unsigned at,
                   unsigned want_mask, unsigned want_at)
{
    if (mask != want_mask || at != want_at)
    {
        printf("FAIL: %s: mask %08x at %08x, wanted %08x / %08x\n",
               what, mask, at, want_mask, want_at);
        failures++;
    }
}

/*
 * The formulas, kept here as the C they were lifted from -- the same
 * two functions rs_gfx() carries in kernel_init, word for word.
 */
static int bd_dark(uint16_t c)
{
    unsigned r = (unsigned)((c >> 11) & 31);
    unsigned g = (unsigned)((c >> 5) & 63);
    unsigned b = (unsigned)(c & 31);

    return (r * 10u + g * 10u + b * 4u) < 600u;
}

static unsigned aero_alpha(int i, int h, unsigned glass)
{
    int op = 255 - (int)glass * 2;
    int a;

    if (op < 40)
        op = 40;
    if (op > 255)
        op = 255;

    a = op - (i < 4 ? (4 - i) * 7 : 0) + (i >= h - 4 ? 14 : 0);
    if (a < 16)
        a = 16;
    if (a > 255)
        a = 255;
    return (unsigned)a;
}

/*
 * The scene's paint in the word forms gfx.rs packs -- copied from
 * rs_gfx(), which took them from wallpaper(), aero_field() and
 * start_orb().  Mutable, because the falsification below corrupts
 * them one word at a time and puts each back.
 */
static unsigned bloom[PAINT_WORDS] = {
    (40u << 20) | (58u << 10) | 150u,               (0u << 8) | 104u,
    (192u << 20) | (138u << 10) | 150u,             (1u << 8) | 112u,
    (342u << 20) | (240u << 10) | 150u,             (2u << 8) | 118u,
    (472u << 20) | (356u << 10) | 150u,             (1u << 8) | 112u,
    (604u << 20) | (482u << 10) | 150u,             (0u << 8) | 104u,
    (340u << 20) | (238u << 10) | 74u,              (3u << 8) | 76u,
    (470u << 20) | (354u << 10) | 62u,              (3u << 8) | 64u,
    (1u << 30) | (300u << 20) | (452u << 10) | 250u,  (0u << 8) | 165u,
    (1u << 30) | (556u << 20) | (476u << 10) | 210u,  (1u << 8) | 175u,
    (1u << 30) | (596u << 20) | (58u << 10) | 170u,   (2u << 8) | 185u,
    (2u << 30) | (215u << 20) | (315u << 10) | 165u,  (1u << 8) | 96u,
    (3u << 30) | (110u << 20) | (210u << 10) | 210u,  0u,
    (3u << 30) | (111u << 20) | (429u << 10) | 3u,    (5u << 8) | 0u,
    (3u << 30) | (110u << 20) | (428u << 10) | 3u,    (4u << 8) | 0u,
};

/* The palette, in gfx.rs's order: glass, rim, button, act, ink,
 * shadow, teal, the orb's three blues, the three glows, the grid,
 * the aurora's three, the lit grid, Workbench's shade and grey,
 * the pearl, the teal and the navy of the wordmark. */
static unsigned pal[PAL_N] = {
    NB_RGB(1, 3, 6),      NB_RGB(11, 26, 15),  NB_RGB(7, 16, 12),
    NB_RGB(11, 25, 17),   NB_RGB(31, 63, 31),  NB_RGB(0, 1, 1),
    NB_RGB(6, 41, 22),    NB_RGB(9, 44, 31),   NB_RGB(1, 20, 30),
    NB_RGB(0, 8, 16),     NB_RGB(19, 53, 25),  NB_RGB(20, 53, 28),
    NB_RGB(19, 56, 25),   NB_RGB(21, 58, 28),  NB_RGB(8, 44, 30),
    NB_RGB(20, 58, 31),   NB_RGB(14, 52, 31),  NB_RGB(16, 44, 30),
    NB_RGB(11, 21, 11),   NB_RGB(21, 42, 21),  NB_RGB(30, 61, 30),
    NB_RGB(6, 41, 22),    NB_RGB(2, 15, 10),
};

/* The orb, in the word forms gfx.rs packs: op<<30 | when<<27 |
 * x<<17 | y<<7 | r, then colour<<8 | alpha. */
static unsigned orb[ORB_WORDS] = {
    (1u << 30) | (1u << 27) | (28u << 17) | (501u << 7) | 15u,
    ((unsigned)NB_RGB(6, 41, 22) << 8) | 70u,
    (1u << 30) | (1u << 27) | (28u << 17) | (501u << 7) | 12u,
    ((unsigned)NB_RGB(6, 41, 22) << 8) | 96u,
    (29u << 17) | (503u << 7) | 11u,
    ((unsigned)NB_RGB(0, 1, 1) << 8),
    (28u << 17) | (501u << 7) | 11u,
    ((unsigned)NB_RGB(31, 63, 31) << 8),
    (28u << 17) | (501u << 7) | 10u,
    ((unsigned)NB_RGB(1, 20, 30) << 8),
    (1u << 30) | (2u << 27) | (28u << 17) | (498u << 7) | 8u,
    ((unsigned)NB_RGB(9, 44, 31) << 8) | 140u,
    (1u << 30) | (1u << 27) | (28u << 17) | (498u << 7) | 8u,
    ((unsigned)NB_RGB(9, 44, 31) << 8) | 164u,
    (1u << 30) | (28u << 17) | (505u << 7) | 7u,
    ((unsigned)NB_RGB(0, 8, 16) << 8) | 96u,
    (2u << 27) | (28u << 17) | (501u << 7) | 6u,
    ((unsigned)NB_RGB(30, 61, 30) << 8),
    (1u << 27) | (28u << 17) | (501u << 7) | 6u,
    ((unsigned)NB_RGB(6, 41, 22) << 8),
    (2u << 30) | (23u << 17) | (496u << 7) | 10u,  0u,
    (3u << 27) | (29u << 17) | (503u << 7) | 11u,
    ((unsigned)NB_RGB(11, 21, 11) << 8),
    (3u << 27) | (28u << 17) | (501u << 7) | 11u,
    ((unsigned)NB_RGB(31, 63, 31) << 8),
    (3u << 27) | (28u << 17) | (501u << 7) | 10u,
    ((unsigned)NB_RGB(21, 42, 21) << 8),
    (1u << 30) | (3u << 27) | (28u << 17) | (498u << 7) | 8u,
    ((unsigned)NB_RGB(31, 63, 31) << 8) | 70u,
    (3u << 27) | (28u << 17) | (501u << 7) | 6u,
    ((unsigned)NB_RGB(30, 61, 30) << 8),
    (2u << 30) | (3u << 27) | (22u << 17) | (495u << 7) | 12u,  0u,
};

/* rs_gfx()'s reference, rebuilt for whatever glass and backdrop the
 * sweep below hands it. */
static void build(unsigned glass, uint16_t top, uint16_t bot,
                  unsigned *aero, unsigned *layer,
                  unsigned *dark_top, unsigned *dark_bot)
{
    unsigned r0, a0;
    int i;

    *dark_top = bd_dark(top) ? 1u : 0u;
    *dark_bot = bd_dark(bot) ? 1u : 0u;

    for (i = 0; i < (int)AERO_N; i++)
        aero[i] = aero_alpha(i, (int)AERO_N, glass);

    r0 = 3u + ((unsigned)top % 251u);
    a0 = 5u + ((unsigned)bot & 0xffu);
    for (i = 1; i <= (int)LAYER_N; i++)
        layer[i - 1] = ((r0 * (unsigned)i) / 5u) << 8 | (a0 / 5u);
}

/* The very call kernel_init makes, over the lengths the check takes
 * as itself. */
static unsigned run(unsigned glass, uint16_t top, uint16_t bot,
                    unsigned dark_top, unsigned dark_bot,
                    const unsigned *aero, const unsigned *bloom,
                    const unsigned *pal, const unsigned *layer,
                    const unsigned *orb, unsigned *at)
{
    return nb_rs_gfx_check(glass, top, bot, dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS, at);
}

/* The orb's `when`, as orb_applies() reads it -- kept here so the
 * expected `at` of an orb falsification can be predicted rather
 * than observed. */
static int applies(unsigned when, int aero, int open)
{
    if (when == 0u) return 1;              /* always */
    if (when == 1u) return aero && open;   /* menu open  */
    if (when == 2u) return aero && !open;  /* menu closed */
    return !aero;                          /* classic bar, and bad tables */
}

/* The first state the battery walks (aero closed, aero open,
 * classic) in which this entry draws, packed the way `at` packs it
 * for a word mismatch. */
static unsigned state_at(const unsigned *orb, unsigned e)
{
    unsigned when = (orb[2 * e] >> 27) & 7u;

    if (applies(when, 1, 0)) return 0u;
    if (applies(when, 1, 1)) return 1u << 8;
    return 2u << 8;
}

int main(void)
{
    /* Night, day, the weight line (598 dark beside 600 light), and
     * a mixed pair -- every one of which both halves must read the
     * same way. */
    static const struct {
        uint16_t top, bot;
        const char *name;
    } backdrops[] = {
        { NB_RGB(1, 3, 6),    NB_RGB(0, 1, 1),    "night" },
        { NB_RGB(31, 63, 31), NB_RGB(30, 61, 30), "day" },
        { NB_RGB(31, 28, 2),  NB_RGB(30, 30, 0),  "weight 598 and 600" },
        { NB_RGB(31, 63, 31), NB_RGB(0, 1, 1),    "mixed" },
    };
    static const unsigned glasses[] = {
        0u, 1u, 20u, 40u, 71u, 100u, 108u, 127u, 254u, 255u
    };

    unsigned aero[AERO_N];
    unsigned layer[LAYER_N];
    unsigned dark_top, dark_bot, mask, at;
    unsigned i, g, d;
    char what[96];

    /* The formulas pinned, not merely agreed: the same numbers
     * gfx.rs's aero_pins test holds, and the two weights bd_dark()
     * turns on between.  Agreement alone would pass two halves that
     * were both wrong; these pin the C side to values the Rust side
     * is separately pinned to. */
    ok(aero_alpha(0, 22, 20) == 187, "row one at the shipping glass");
    ok(aero_alpha(3, 22, 20) == 208, "row four eases to 208");
    ok(aero_alpha(4, 22, 20) == 215, "row five is the plain base");
    ok(aero_alpha(17, 22, 20) == 215, "the middle stays level");
    ok(aero_alpha(18, 22, 20) == 229, "the last four lift by 14");
    ok(aero_alpha(21, 22, 20) == 229, "the very last row lifts too");
    ok(aero_alpha(0, 22, 0) == 227, "an invisible glass is opaque");
    ok(aero_alpha(0, 22, 100) == 27, "a heavy glass eases to 27");
    ok(aero_alpha(21, 22, 100) == 69, "and lifts to 69");
    ok(aero_alpha(0, 22, 108) == 16, "an impossible glass lands at 16");
    ok(bd_dark(NB_RGB(31, 28, 2)), "weight 598 is a night");
    ok(!bd_dark(NB_RGB(30, 30, 0)), "weight 600 is not");

    /* Agreement, over every glass against every backdrop: each pair
     * must come back with nothing to report and `at` zeroed by the
     * entry, not merely left as it was found. */
    for (d = 0; d < sizeof backdrops / sizeof backdrops[0]; d++)
    {
        for (g = 0; g < sizeof glasses / sizeof glasses[0]; g++)
        {
            build(glasses[g], backdrops[d].top, backdrops[d].bot,
                  aero, layer, &dark_top, &dark_bot);
            at = SENTINEL;
            mask = run(glasses[g], backdrops[d].top, backdrops[d].bot,
                       dark_top, dark_bot, aero, bloom, pal, layer,
                       orb, &at);
            snprintf(what, sizeof what, "glass %u over %s",
                     glasses[g], backdrops[d].name);
            expect(what, mask, at, 0u, 0u);
        }
    }

    /* One flipped seed: the night-field bit, naming the top and then
     * the bot colour it doubted. */
    build(20, backdrops[0].top, backdrops[0].bot,
          aero, layer, &dark_top, &dark_bot);

    at = SENTINEL;
    mask = run(20, backdrops[0].top, backdrops[0].bot,
               dark_top ^ 1u, dark_bot, aero, bloom, pal, layer,
               orb, &at);
    expect("a flipped top seed names the top", mask, at, F_DARK, 0u);

    at = SENTINEL;
    mask = run(20, backdrops[0].top, backdrops[0].bot,
               dark_top, dark_bot ^ 1u, aero, bloom, pal, layer,
               orb, &at);
    expect("a flipped bot seed names the bot", mask, at, F_DARK, 1u);

    /* Every row of the bar, one at a time: F_AERO with the row. */
    for (i = 0; i < AERO_N; i++)
    {
        aero[i] ^= 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "bar row %u corrupted", i);
        expect(what, mask, at, F_AERO, i);
        aero[i] ^= 1u;
    }

    /* Every rung of the glow ladder. */
    for (i = 0; i < LAYER_N; i++)
    {
        layer[i] ^= 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "glow rung %u corrupted", i);
        expect(what, mask, at, F_LAYER, i);
        layer[i] ^= 1u;
    }

    /* Every word of the bloom table, with `at` the word itself. */
    for (i = 0; i < PAINT_WORDS; i++)
    {
        bloom[i] ^= 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "bloom word %u corrupted", i);
        expect(what, mask, at, F_BLOOM, i);
        bloom[i] ^= 1u;
    }

    /* Every colour in the palette. */
    for (i = 0; i < PAL_N; i++)
    {
        pal[i] ^= 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "palette word %u corrupted", i);
        expect(what, mask, at, F_PAL, i);
        pal[i] ^= 1u;
    }

    /* The orb, three ways.  Flipping a `when` moves the count at
     * state 0 first, so `at` is the count's mark there; corrupting
     * either word of an entry leaves the filters alone, so the walk
     * pairs it where it always did and `at` is that entry's slot in
     * the first state it draws in. */
    for (i = 0; i < ORB_N; i++)
    {
        unsigned w = orb[2 * i];
        unsigned when = (w >> 27) & 7u;

        orb[2 * i] = (w & ~(7u << 27)) | (((when ^ 1u) & 7u) << 27);
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "orb entry %u in the wrong state", i);
        expect(what, mask, at, F_ORB, 0x80u);
        orb[2 * i] = w;

        orb[2 * i] = w ^ 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "orb entry %u shape corrupted", i);
        expect(what, mask, at, F_ORB, state_at(orb, i) | i);
        orb[2 * i] = w;

        orb[2 * i + 1] ^= 1u;
        at = SENTINEL;
        mask = run(20, backdrops[0].top, backdrops[0].bot,
                   dark_top, dark_bot, aero, bloom, pal, layer,
                   orb, &at);
        snprintf(what, sizeof what, "orb entry %u paint corrupted", i);
        expect(what, mask, at, F_ORB, state_at(orb, i) | i);
        orb[2 * i + 1] ^= 1u;
    }

    /* Two families at once: both bits, and `at` the first word the
     * check walks to -- the bar runs before the palette. */
    aero[5] ^= 1u;
    pal[3] ^= 1u;
    at = SENTINEL;
    mask = run(20, backdrops[0].top, backdrops[0].bot,
               dark_top, dark_bot, aero, bloom, pal, layer, orb, &at);
    expect("two families, the first word named", mask, at,
           F_AERO | F_PAL, 5u);
    aero[5] ^= 1u;
    pal[3] ^= 1u;

    /* Bad calls: every length off by one says which it was, a null
     * says the family that is missing, and no `at` cannot stand. */
    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N - 1u, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS, &at);
    expect("a short bar cannot stand", mask, at, F_BAD, 0x0bad0001u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS - 1u,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS, &at);
    expect("a short bloom cannot stand", mask, at, F_BAD, 0x0bad0002u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N - 1u, layer, LAYER_N,
                           orb, ORB_WORDS, &at);
    expect("a short palette cannot stand", mask, at, F_BAD, 0x0bad0003u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N - 1u,
                           orb, ORB_WORDS, &at);
    expect("a short ladder cannot stand", mask, at, F_BAD, 0x0bad0004u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS - 1u, &at);
    expect("a short orb cannot stand", mask, at, F_BAD, 0x0bad0005u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           NULL, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS, &at);
    expect("no bar cannot stand", mask, at, F_BAD, 0x0bad0000u);

    at = SENTINEL;
    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           NULL, ORB_WORDS, &at);
    expect("no orb cannot stand", mask, at, F_BAD, 0x0bad0000u);

    mask = nb_rs_gfx_check(20, backdrops[0].top, backdrops[0].bot,
                           dark_top, dark_bot,
                           aero, AERO_N, bloom, PAINT_WORDS,
                           pal, PAL_N, layer, LAYER_N,
                           orb, ORB_WORDS, NULL);
    ok(mask == BAD_CALL, "nowhere to write `at` cannot stand");

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_gfx: all checks passed\n");
    return 0;
}
