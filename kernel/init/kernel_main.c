#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/audio.h"
#include "../../boot/rom/kbd.h"
#include "../../boot/rom/prefs.h"
#include "../../boot/rom/pfs.h"
#include "../../boot/rom/gfx.h"
#include "../../user/gui/desktop/progs.h"

extern void kernel_banner(void);
extern void kernel_detect(void);
extern void kernel_drivers(void);
extern void kernel_ok(const char *msg);
extern void kernel_warn(const char *msg);
extern void kernel_ok_sound(unsigned rate, unsigned vol);
extern void kernel_starting(const char *unit);
extern void kernel_started(const char *unit);
extern void kernel_target(const char *target);
extern void nb_desktop_render(void);
extern void nb_desktop_tick(void);
extern int  nb_desktop_click(int x, int y, int btn);
extern int  nb_desktop_drag(void);
extern int  nb_desktop_key(int c);
extern void nb_pointer_enable(void);
extern void nb_pointer_after_present(void);
extern int  nb_pointer_frame(void);
extern int  nb_pointer_x(void);
extern int  nb_pointer_y(void);
extern void nb_pointer_dump(void);
extern void nb_desktop_dump(void);
extern int  nb_desktop_exit(void);
extern int  nb_shell_boot(void);
extern unsigned nb_rs_selftest(unsigned seed);

/*
 * The Rust half's parser: the four Config files as they stand in the
 * store, the struct prefs.c built out of them, and back a mask of the
 * fields where the two disagreed -- one bit each, in struct order
 * (libs/nb_rs/src/prefs.rs has the table), with bit 31 alone meaning
 * the call itself was unusable.
 */
extern unsigned nb_rs_prefs_check(const unsigned char **files,
                                  const unsigned *lens,
                                  const struct nb_prefs *have);

/*
 * The Rust half's store: the table itself, then six batteries of
 * questions -- the paths to try, the per-directory children walk, the
 * program table, program_slot_of for every node, a `run` battery of
 * arguments, and sampled pfs_next_child answers -- each with the C
 * half's own answers beside them, and back a mask naming the family
 * where the two came apart (libs/nb_rs/src/store.rs has the table),
 * with `at` saying where inside it.  The safety of the reads is this
 * side's contract: everything handed over is one of our own static
 * arrays or the ROM's own table.
 */
extern unsigned nb_rs_store_check(const struct pfs_node *nodes,
                                  unsigned ncount,
                                  const char *const *queries, unsigned nq,
                                  const unsigned *hits,
                                  const unsigned *seq, unsigned nseq,
                                  const char *const *names, unsigned nnames,
                                  const unsigned *slots, unsigned nslots,
                                  const char *const *args,
                                  const unsigned *alens, unsigned nargs,
                                  const unsigned *want,
                                  const unsigned *nxt,
                                  const unsigned *nxt_hit, unsigned nnxt,
                                  unsigned *at);

/*
 * The Rust half's Vista chrome: the formulas the glass and the aurora
 * are computed from, and the three tables the scene is drawn out of
 * -- the paint (aurora, glows, the bloom behind the mark, the
 * branding), the palette, and the orb in its states.  This side
 * passes its own reference for every one of them, written out here
 * from the numbers main.c draws the rest of the chrome with, plus the
 * Config's own glass and the backdrop's two colours -- values no
 * compiler has seen before this boot -- and back comes a mask naming
 * the family where the two halves came apart (libs/nb_rs/src/gfx.rs
 * has the table of bits), with `at` saying where inside it.  Same
 * contract as the store: everything handed over is one of our own
 * static arrays.
 */
extern unsigned nb_rs_gfx_check(unsigned glass,
                                unsigned top, unsigned bot,
                                unsigned dark_top, unsigned dark_bot,
                                const unsigned *aero, unsigned naero,
                                const unsigned *bloom, unsigned nbloom,
                                const unsigned *pal, unsigned npal,
                                const unsigned *layer, unsigned nlayer,
                                const unsigned *orb, unsigned norb,
                                unsigned *at);

/*
 * struct nb_prefs is read across this line, by repr(C) on the Rust
 * side and by this compiler on ours, so both hold it to the numbers
 * that line was written against: an int aligns at two bytes on
 * m68k-linux-gnu (four on a host, where tools/tests/test_prefs.c
 * asserts its own pair), everything after `colour` lands accordingly,
 * and the whole struct is 150 bytes.  rustc asserts the same numbers
 * in prefs.rs; if either side moves, a build stops here rather than a
 * boot comparing the wrong bytes as fact.
 */
_Static_assert(__builtin_offsetof(struct nb_prefs, start_x) == 82,
               "struct nb_prefs: start_x moved");
_Static_assert(__builtin_offsetof(struct nb_prefs, snd_file) == 98,
               "struct nb_prefs: snd_file moved");
_Static_assert(__builtin_offsetof(struct nb_prefs, hold) == 138,
               "struct nb_prefs: hold moved");
_Static_assert(sizeof(struct nb_prefs) == 150,
               "struct nb_prefs: size moved");

/*
 * struct pfs_node is read across the phase-2 line the same way: this
 * compiler lays it out at four bytes a field with no padding -- an
 * int aligns at two bytes on m68k-linux-gnu and every member is
 * either a pointer or an unsigned, so nothing shows through -- and
 * rustc asserts the same six numbers in store.rs.  The host test
 * holds the third pair (tools/tests/test_store.c).  If a member
 * moves, a build stops here rather than a boot walking a table at
 * the wrong offsets.
 */
_Static_assert(__builtin_offsetof(struct pfs_node, path) == 4,
               "struct pfs_node: path moved");
_Static_assert(__builtin_offsetof(struct pfs_node, parent) == 8,
               "struct pfs_node: parent moved");
_Static_assert(__builtin_offsetof(struct pfs_node, data) == 12,
               "struct pfs_node: data moved");
_Static_assert(__builtin_offsetof(struct pfs_node, size) == 16,
               "struct pfs_node: size moved");
_Static_assert(__builtin_offsetof(struct pfs_node, dir) == 20,
               "struct pfs_node: dir moved");
_Static_assert(sizeof(struct pfs_node) == 24,
               "struct pfs_node: size moved");

/* ------------------------------------------------------------------ *
 * The Rust half's read-out: serial only, the line nb_prefs_dump()      *
 * already writes, and never a second of the boot log.                  *
 * ------------------------------------------------------------------ */

static void rs_wire(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

/* Exactly eight hexadecimal digits, most significant first. */
static void rs_mask(unsigned v)
{
    static const char dig[] = "0123456789abcdef";
    int i;

    for (i = 7; i >= 0; i--)
        amiga_serial_putc(dig[(v >> (i * 4)) & 15u]);
}

/*
 * The parser held against the parser.  The kernel hands the Rust half
 * the same four files nb_prefs_load() was given, straight out of the
 * store, and the struct it produced, and the mask that comes back
 * names -- bit by bit -- the fields where the two parses ended up
 * apart.  A pass writes one serial line, the way the codegen selftest
 * above reports, and never appears in the log; a disagreement earns
 * both the mask on the wire and the one amber line, so the boot's own
 * counts catch it like any other fault.
 */
static void rs_prefs(void)
{
    static const char *const cfg[4] = {
        "Config/screen.cfg", "Config/pointer.cfg",
        "Config/sound.cfg",  "Config/boot.cfg"
    };
    const unsigned char *files[4];
    unsigned lens[4];
    unsigned rs, i;

    for (i = 0; i < 4; i++)
    {
        const struct pfs_node *n = pfs_find(cfg[i]);

        /* A directory wearing the name is not a file to read, which
         * is the same test nb_prefs_load() makes on this side. */
        if (n && !n->dir)
        {
            files[i] = n->data;
            lens[i]  = n->size;
        }
        else
        {
            files[i] = 0;
            lens[i]  = 0;
        }
    }

    rs = nb_rs_prefs_check(files, lens, &nb_prefs);
    rs_wire(">rs prefs ");
    if (rs)
    {
        rs_wire("fail mask=");
        rs_mask(rs);
        rs_wire("\r\n");
        kernel_warn("Rust prefs parse");
    }
    else
        rs_wire("ok\r\n");
}

/*
 * What the phase-2 batteries are sized for: a store up to RS_N nodes
 * (the ROM ships with a fraction of that), RS_E edge paths beside the
 * table's own paths, RS_AR arguments for the `run` battery, and
 * RS_NX next-child samples for each of up to RS_DIRS directories.
 */
#define RS_N     128
#define RS_E      15
#define RS_Q      (RS_N + RS_E)
#define RS_AR     48
#define RS_DIRS   32
#define RS_NX      4

/*
 * The store held against the store -- phase 2.  The kernel hands the
 * Rust half the same table pfs_find() walks and the same files
 * program_slot_of() reads, and with every battery the C's own answers
 * beside it: the paths (the table's own first, then the edges that
 * pin the first-byte rule), one children segment per node, the
 * program table, a slot per node, a `run` battery of arguments, and
 * four pfs_next_child samples per directory over `after' values the
 * walk never stands on -- the wrap, the directory's own index, the
 * top and the tail.  The mask that comes back names the family where
 * the two halves came apart and `at` says where inside it; a pass
 * writes one serial line, like rs_prefs() above, and never appears in
 * the log.  The batteries are static because the kernel has no heap
 * to borrow, and sized for a store four times the one the ROM ships
 * with -- a table that outgrew them says so as an unusable call
 * rather than checking half of one.
 */
static void rs_store(void)
{
    /* The edge paths: "" and "/" are the root, "/Config" is the root
     * too -- pfs_find's first byte says so, whatever its comment
     * says -- and everything else is a miss spelled carefully. */
    static const char *const edge[] = {
        0, "", "/", "/Config", "/Core/Docs/About", "Config/",
        "config/screen.cfg", "Config/screen.cfg/", "No/such/file",
        "Tools/Clock/", "Core/Docs/About.txt", "Core/Docs/About ",
        "Temp/../Temp", "\tConfig", "Core"
    };

    /* What `run <arg>` is asked, beside the eight from the table. */
    static const char *const rv[] = {
        "Files", "FILES", "file", "files2", "clock", "Clock",
        " clock", "clock ", "", "prefer", "PREFERENCES", "about.txt",
        "0clock", "neotext", "NeoText", "NEOSHELL", "vlc", "VLC",
        "monitor", "MONITOR", "Monitor", "NeoShell", "preference",
        "preferencesx", "pro gram", "cloc k"
    };

    static const char *queries[RS_Q];
    static const char *av[RS_AR];
    static unsigned hits[RS_Q];
    static unsigned seq[3 * RS_N];
    static unsigned slots[RS_N];
    static unsigned alens[RS_AR];
    static unsigned want[RS_AR];
    static unsigned nxt[RS_DIRS * RS_NX * 2];
    static unsigned nxt_hit[RS_DIRS * RS_NX];

    const unsigned ncount = nb_pfs_count;
    unsigned nq = 0, nseq = 0, nargs = 0, nnxt = 0, nd = 0;
    unsigned i, d, c, mask, at;

    if (ncount == 0 || ncount > RS_N ||
        RS_E > RS_Q || ncount + RS_E > RS_Q)
    {
        rs_wire(">rs store fail mask=");
        rs_mask(0x80000000u);
        rs_wire(" at=");
        rs_mask(0);
        rs_wire("\r\n");
        kernel_warn("Rust store check");
        return;
    }

    /* The paths battery: the table's own paths first -- the check
     * takes that as the contract behind nq >= ncount -- then the
     * edges, with this half's pfs_find answers either way. */
    for (i = 0; i < ncount; i++)
    {
        const struct pfs_node *n;

        queries[nq] = nb_pfs_nodes[i].path;
        n = pfs_find(queries[nq]);
        hits[nq] = n ? (unsigned)(n - nb_pfs_nodes) : PFS_NONE;
        nq++;
    }
    for (i = 0; i < RS_E; i++)
    {
        const struct pfs_node *n;

        queries[nq] = edge[i];
        n = pfs_find(edge[i]);
        hits[nq] = n ? (unsigned)(n - nb_pfs_nodes) : PFS_NONE;
        nq++;
    }

    /* The walk, in the shape the check walks from its side: header,
     * children, NONE, once for every node in order.  Worst case is
     * three entries a node -- header, one child each, one NONE. */
    for (d = 0; d < ncount; d++)
    {
        seq[nseq++] = d;
        c = pfs_first_child(d);
        while (c != PFS_NONE)
        {
            seq[nseq++] = c;
            c = pfs_next_child(d, c);
        }
        seq[nseq++] = PFS_NONE;
    }

    for (i = 0; i < ncount; i++)
        slots[i] = (unsigned)program_slot_of(i);

    /* The run battery: the eight table names as themselves, then the
     * cases -- case, prefix, trailing space, the empty argument --
     * with this half's first-match answer for each, made with the
     * same tok_is sh_run_cmd() asks sh_is(). */
    for (i = 0; i < N_PROGRAMS; i++)
        av[nargs++] = prog_name[i];
    for (i = 0; i < (unsigned)(sizeof rv / sizeof rv[0]) &&
                nargs < RS_AR; i++)
        av[nargs++] = rv[i];
    for (i = 0; i < nargs; i++)
    {
        const unsigned char *a = (const unsigned char *)av[i];
        const unsigned char *e = a;

        while (*e)
            e++;
        alens[i] = (unsigned)(e - a);
        want[i] = PFS_NONE;
        for (d = 0; d < N_PROGRAMS; d++)
            if (tok_is(a, e, prog_name[d]))
            {
                want[i] = d;
                break;
            }
    }

    /* The next-child samples: four `after' values a walk would never
     * actually stand on, for every directory that fits. */
    for (d = 0; d < ncount && nd < RS_DIRS; d++)
    {
        unsigned k;

        if (!nb_pfs_nodes[d].dir)
            continue;
        for (k = 0; k < RS_NX; k++)
        {
            unsigned after;

            switch (k)
            {
            case 0:  after = PFS_NONE;   break;    /* the wrap        */
            case 1:  after = d;          break;    /* its own index   */
            case 2:  after = 0;          break;    /* the top         */
            default: after = ncount - 1; break;    /* the tail        */
            }
            nxt[nnxt * 2]     = d;
            nxt[nnxt * 2 + 1] = after;
            nxt_hit[nnxt]     = pfs_next_child(d, after);
            nnxt++;
        }
        nd++;
    }

    mask = nb_rs_store_check(nb_pfs_nodes, ncount,
                             queries, nq, hits,
                             seq, nseq,
                             prog_name, N_PROGRAMS,
                             slots, ncount,
                             av, alens, nargs, want,
                             nxt, nxt_hit, nnxt, &at);
    rs_wire(">rs store ");
    if (mask)
    {
        rs_wire("fail mask=");
        rs_mask(mask);
        rs_wire(" at=");
        rs_mask(at);
        rs_wire("\r\n");
        kernel_warn("Rust store check");
    }
    else
        rs_wire("ok\r\n");
}

/*
 * The formulas the chrome is computed from, kept here as the C they
 * were lifted from -- bd_dark() and aero_field() in main.c, and the
 * ladder glow() used to climb.  Two copies on purpose: only a second
 * copy catches a retyping, and this one is what the Rust half is
 * held against.
 */
static int gfx_bd_dark(uint16_t c)
{
    unsigned r = (unsigned)((c >> 11) & 31);
    unsigned g = (unsigned)((c >> 5) & 63);
    unsigned b = (unsigned)(c & 31);

    return (r * 10u + g * 10u + b * 4u) < 600u;
}

static unsigned gfx_aero_alpha(int i, int h, unsigned glass)
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
 * The paint held against the paint.  Phase 3 moved the scene the boot
 * leaves on screen -- the wash and its aurora, the glass bar, the
 * orb -- into libs/nb_rs/src/gfx.rs, and this is the C standing over
 * it: every formula above and every table below, written out from the
 * numbers wallpaper(), aero_field() and start_orb() carried, with the
 * Config's own glass and backdrop resolving to colours nothing knew
 * when this compiled, so each leg stands on an unknown value.  One
 * bit a family, `at` inside it; serial alone when they agree, like
 * the batteries before it.
 */
static void rs_gfx(void)
{
    /*
     * The scene's paint in the word forms gfx.rs packs: kind<<30 |
     * x<<20 | y<<10 | r, then slot<<8 | alpha.  Kind 0 is the aurora,
     * 1 the older three glows, 2 the bloom that lights the mark, 3
     * the branding (mark, wordmark shadow, wordmark -- the mark's own
     * colours are its artwork's, so its second word is zero).
     */
    static const unsigned bloom[] = {
        (40u << 20) | (58u << 10) | 150u,          (0u << 8) | 104u,
        (192u << 20) | (138u << 10) | 150u,        (1u << 8) | 112u,
        (342u << 20) | (240u << 10) | 150u,        (2u << 8) | 118u,
        (472u << 20) | (356u << 10) | 150u,        (1u << 8) | 112u,
        (604u << 20) | (482u << 10) | 150u,        (0u << 8) | 104u,
        (340u << 20) | (238u << 10) | 74u,         (3u << 8) | 76u,
        (470u << 20) | (354u << 10) | 62u,         (3u << 8) | 64u,
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
    static const unsigned pal[] = {
        NB_RGB(1, 3, 6),      NB_RGB(11, 26, 15),  NB_RGB(7, 16, 12),
        NB_RGB(11, 25, 17),   NB_RGB(31, 63, 31),  NB_RGB(0, 1, 1),
        NB_RGB(6, 41, 22),    NB_RGB(9, 44, 31),   NB_RGB(1, 20, 30),
        NB_RGB(0, 8, 16),     NB_RGB(19, 53, 25),  NB_RGB(20, 53, 28),
        NB_RGB(19, 56, 25),   NB_RGB(21, 58, 28),  NB_RGB(8, 44, 30),
        NB_RGB(20, 58, 31),   NB_RGB(14, 52, 31),  NB_RGB(16, 44, 30),
        NB_RGB(11, 21, 11),   NB_RGB(21, 42, 21),  NB_RGB(30, 61, 30),
        NB_RGB(6, 41, 22),    NB_RGB(2, 15, 10),
    };

    /*
     * The orb, in the word forms gfx.rs packs: op<<30 | when<<27 |
     * x<<17 | y<<7 | r, then colour<<8 | alpha.  when is 0 always,
     * 1 menu open, 2 menu closed, 3 classic bar only -- the aero
     * sphere first (halo, shade, rim, body, lit top, foot, pearl,
     * mark), then the Workbench bevel.
     */
    static const unsigned orb[] = {
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

    unsigned aero[22];
    unsigned layer[5];
    unsigned dark_top, dark_bot, r0, a0, mask, at = 0;
    uint16_t top, bot;
    int i;

    nb_bd_colours(nb_prefs.backdrop, &top, &bot);
    dark_top = gfx_bd_dark(top) ? 1u : 0u;
    dark_bot = gfx_bd_dark(bot) ? 1u : 0u;

    for (i = 0; i < 22; i++)
        aero[i] = gfx_aero_alpha(i, 22, nb_prefs.bar_glass);

    /* The glow ladder's samples turn with the two colours, exactly as
     * gfx.rs derives them: five passes of one radius. */
    r0 = 3u + ((unsigned)top % 251u);
    a0 = 5u + ((unsigned)bot & 0xffu);
    for (i = 1; i <= 5; i++)
        layer[i - 1] = ((r0 * (unsigned)i) / 5u) << 8 | (a0 / 5u);

    mask = nb_rs_gfx_check(nb_prefs.bar_glass,
                           (unsigned)top, (unsigned)bot,
                           dark_top, dark_bot,
                           aero, (unsigned)(sizeof aero / sizeof aero[0]),
                           bloom, (unsigned)(sizeof bloom / sizeof bloom[0]),
                           pal, (unsigned)(sizeof pal / sizeof pal[0]),
                           layer, (unsigned)(sizeof layer / sizeof layer[0]),
                           orb, (unsigned)(sizeof orb / sizeof orb[0]),
                           &at);
    rs_wire(">rs gfx ");
    if (mask)
    {
        rs_wire("fail mask=");
        rs_mask(mask);
        rs_wire(" at=");
        rs_mask(at);
        rs_wire("\r\n");
        kernel_warn("Rust gfx check");
    }
    else
        rs_wire("ok\r\n");
}

/*
 * The interactive phase: the receivers polled every field, and the
 * scene recomposited only when an event actually changed something.
 *
 * A press that stays down is a different case and is asked for
 * separately, because it lasts longer than the field it arrived on.
 * It picks a window up by its caption and carries it for as long as
 * the button is held; the answer is the same one a press gives --
 * only when the rows on screen would change -- so it takes the same
 * three calls behind it, and a pointer that has not moved between
 * two fields costs the two reads that took to find out.
 *
 * The phase ends the one way it can end from the inside: the start
 * menu's Exit to shell sets the flag this reads at the top of each
 * field, and the return hands the machine back to whoever started
 * the phase -- which is the shell, as it was at boot.  Reboot and
 * Shut down never get here: they take the screen over on their way
 * out and do not come back.
 */
static void desktop_loop(void)
{
    for (;;)
    {
        int pressed;
        int c;

        if (nb_desktop_exit())  /* Exit to shell: walk back       */
            return;

        nb_sound_poll();

        /*
         * The wait for the next field is where the receivers are read.
         * Paula carries one received byte and the next arrival overwrites
         * it, so a character typed between two fields has to be taken
         * here rather than 20 ms later, and the keyboard is read in the
         * same loop because the queue is what the two have in common.
         *
         * The one stretch nothing reads is a present, which lasts as long
         * as the repaint behind it: the keyboard's code waits for the
         * handshake either way, and the byte on the wire is the one that
         * can be lost there.
         */
        while (!amiga_vbl_pending())
            nb_kbd_poll();

        /*
         * What was typed, from whichever receiver it came from, handed to
         * the scene as a key.  The scene takes it the way it takes a
         * press and says whether anything changed, so a key that lands
         * where nothing is lit costs no present.
         */
        while ((c = nb_kbd_get()) >= 0)
        {
            if (nb_desktop_key(c))
            {
                nb_desktop_render();
                nb_pointer_after_present();
                nb_desktop_dump();      /* which programs are on screen     */
            }
        }

        pressed = nb_pointer_frame();

        if (pressed &&
            nb_desktop_click(nb_pointer_x(), nb_pointer_y(), pressed))
        {
            nb_desktop_render();
            nb_pointer_after_present();
            nb_desktop_dump();          /* which programs are on screen */
        }

        /*
         * And the press that was still down when this field came
         * round: the window it came down on is carried wherever the
         * pointer has got to, and let go when the button is.
         */
        if (nb_desktop_drag())
        {
            nb_desktop_render();
            nb_pointer_after_present();
            nb_desktop_dump();          /* where the window has got to  */
        }

        /*
         * And the one thing that moves without anybody touching it: the
         * player's film steps on the field counter rather than on a key,
         * so it is asked here, once a field, whether anything is due.  It
         * answers with nothing at all nine fields in ten and paints for
         * itself the tenth, so it owes the loop no decision -- only the
         * knowledge that a field happened.
         */
        nb_desktop_tick();
    }
}

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    /*
     * The console has to be up before anything can be said, so the
     * header is printed first and every unit after it is announced
     * before it runs -- the systemd order, where the first thing the log
     * shows is a job being queued rather than a job already finished.
     */
    amiga_serial_putc('K');
    console_init();
    kernel_banner();
    amiga_serial_putc('B');

    /*
     * Preferences come out of the ROM's own file store first of all,
     * before anything is composited and before the two passes below:
     * Config/boot.cfg is what says whether the hardware is walked at
     * all, and the desktop still gets every value in time because it is
     * the last thing drawn.  The read-out goes to serial only -- it is
     * a development line, not part of the boot log.
     */
    kernel_starting("Load Preferences from Config/");
    nb_prefs_load();
    nb_prefs_dump();
    kernel_started("Load Preferences from Config/");

    /*
     * The Rust half of the ROM answers a selftest here, before
     * anything else is trusted to it: six identities -- division with
     * its remainder, signed and unsigned, multiplication against
     * thirty-one additions, rotation against shift-or, byte order in
     * memory, and a loop both halves can count -- each over a value
     * the compiler did not know when it compiled the code, so the
     * test cannot be folded into a constant that always passes.  A
     * wrong instruction or a wrong data layout therefore fails here,
     * on the wire, rather than on a screen; and it says nothing at
     * all to the console, because a selftest that passed has no place
     * in the log.  One amber line is the whole way it appears, and
     * only when the codegen is at fault rather than the machine.
     */
    {
        unsigned rs = nb_rs_selftest(nb_prefs.hold);

        rs_wire(">rs ");
        if (rs)
        {
            rs_wire("fail mask=");
            rs_mask(rs);
            rs_wire("\r\n");
            kernel_warn("Rust codegen selftest");
        }
        else
            rs_wire("ok\r\n");
    }

    /*
     * ... and then the first real module of phase 1 answers for its
     * own work: prefs.rs parses the four files just read the way
     * prefs.c did, and the two halves are held together here over the
     * same bytes (rs_prefs()).  Serial alone when they agree, which is
     * the only outcome this machine has shipped with.
     */
    rs_prefs();

    /*
     * Phase 2 answers after it, over the ground phase 1 stood on: the
     * store's paths, the walk that fills a directory, and the table
     * `progs` and `run` read are each answered by this half and by
     * the C over the same nodes (rs_store()).  Same wire, same rule:
     * serial alone when they agree.
     */
    rs_store();

    /*
     * And phase 3 answers last, over what it paints: the scene this
     * boot leaves on screen -- the wash and its aurora, the glass bar
     * and the orb -- is drawn by gfx.rs, and the C's own copy of
     * every formula and table stands over it here (rs_gfx()).  Same
     * wire, same rule.
     */
    rs_gfx();

    kernel_starting("NeoBench Kernel Initialisation");
    nb_sound_init();
    amiga_serial_putc('C');
    /*
     * Input is armed with the kernel rather than with the desktop,
     * because what it takes away belongs to the ROM underneath: the
     * keyboard's interrupt mask is cleared here, which is the moment the
     * chainloaded system stops seeing keys and NeoBench starts.  It is
     * early and it is deliberately not repeated -- the receivers are
     * polled from the first field onwards, and there is nothing to arm
     * a second time (kbd.c).
     */
    nb_kbd_init();
    nb_kbd_dump();
    kernel_started("NeoBench Kernel Initialisation");

    kernel_starting("Detect Hardware");
    kernel_detect();
    kernel_started("Detect Hardware");
    kernel_target("NeoBench Hardware");
    amiga_serial_putc('D');

    /*
     * The device walk is a preference rather than a fixed step: a
     * machine that wants the log quicker sets scan = off in
     * Config/boot.cfg and gets the one amber line that says so, which
     * is honest about there being no drivers bound instead of quiet
     * about it.
     */
    if (nb_prefs.hwscan)
    {
        kernel_starting("NeoBench Device Drivers");
        kernel_drivers();
        kernel_started("NeoBench Device Drivers");
        kernel_target("NeoBench Devices");
    }
    else
        kernel_warn("Hardware scan disabled by Config/boot.cfg");
    amiga_serial_putc('R');

    /*
     * The startup chime is armed here rather than after the desktop is
     * presented, so Paula is already fetching through the one stretch of
     * boot where the machine looks idle -- the compositor's median cut
     * over the whole back buffer.  The sample comes out of the file
     * store and is copied into chip RAM on the way in: Paula is a DMA
     * master and reads neither the ROM image nor fast RAM.
     *
     * A unit that cannot start prints no second line: the amber tag is
     * the hole, exactly as for a job that never finished.
     */
    if (nb_prefs.snd_startup)
    {
        const struct pfs_node *n = pfs_find(nb_prefs.snd_file);
        unsigned rate = 0;

        kernel_starting("Play Startup Chime");
        if (n && !n->dir)
            rate = nb_sound_play(n->data, n->size, nb_prefs.snd_volume);
        if (rate)
        {
            kernel_ok_sound(rate, nb_prefs.snd_volume);
            kernel_started("Play Startup Chime");
        }
        else
            kernel_warn("No startup chime in the file store");
    }
    else
        kernel_ok("Startup chime disabled by Config/sound.cfg");

    /*
     * The log is then held before the desktop takes the screen over --
     * three seconds by default, from Config/boot.cfg.  The last tags
     * stay legible, the chime is heard out over them rather than cut
     * off by the first composed frame, and the machine reads as having
     * come up rather than as having jumped.  The wait is on the field
     * counter rather than on a loop of no-ops, so it is three seconds
     * however long a frame takes to compose, and the sound is polled
     * through it because that is what the main loop below would do.
     *
     * The hold is a window as well as a pause: Escape at any point in
     * it brings NeoShell up instead of the compositor, which is the
     * failsafe's manual side -- a machine that will not start its
     * desktop, or that is being asked about rather than used, is
     * reached from here with no tool and no rebuild.  Nothing is lost
     * by a key typed early or late: the receivers are polled for the
     * whole of the hold and the queue holds everything they caught.
     *
     * The other two ways in are Config/boot.cfg's own.  `failsafe = on`
     * boots to the shell without asking, for a machine kept for
     * recovery; and a store with no Config/screen.cfg in it has no
     * desktop to composite -- the wash, the bar and the face all come
     * out of that file -- so the shell is where it goes rather than to
     * a screen nothing can be drawn on.  Either way the shell decides
     * when the desktop comes up: `desktop` at the prompt is what
     * returns here, which is what makes it a place to fall back to
     * rather than a place to be stuck in.
     */
    {
        int shell = 0;

        if (!pfs_find("Config/screen.cfg"))
        {
            kernel_warn("Config/screen.cfg missing -- NeoShell");
            shell = 1;
        }
        else if (nb_prefs.failsafe)
        {
            kernel_ok("Failsafe (Config/boot.cfg) -- NeoShell");
            shell = 1;
        }

        if (!shell && nb_prefs.hold)
        {
            uint32_t t0 = nb_fields;
            int esc = 0;
            int c;

            console_set_color(NB_COL_GREY);
            console_write("         Esc for NeoShell\n");
            console_set_color(NB_COL_GREEN);

            while (nb_fields - t0 < nb_prefs.hold * 50u)
            {
                while (!amiga_vbl_pending())
                    nb_kbd_poll();
                while ((c = nb_kbd_get()) >= 0)
                    if (c == NB_KEY_ESC)
                        esc = 1;
                nb_sound_poll();
                if (esc)
                    break;
            }
            if (esc)
                shell = 1;
        }

        /*
         * The desktop and NeoShell are two ends of one road rather
         * than two stages passed through once: failsafe, a store with
         * no screen to draw on, or Escape during the hold all start
         * on the shell side and `desktop` walks across, and the start
         * menu's Exit to shell walks back -- so the pair is a loop,
         * and every visit prints the same G and P a chain boot counts
         * on the first.
         */
        for (;;)
        {
            if (shell)
            {
                kernel_target("NeoShell");
                nb_shell_boot();
                shell = 0;
            }

            /* Desktop scene replaces the boot log on screen. */
            kernel_target("Graphical Interface");
            amiga_serial_putc('G');
            nb_desktop_render();
            nb_pointer_enable();
            nb_pointer_dump();
            amiga_serial_putc('P');

            desktop_loop();         /* returns to NeoShell, or runs for ever */
            shell = 1;
        }
    }
}
