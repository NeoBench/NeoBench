/*
 * Host test of preference parsing: the Rust half held against the C.
 *
 * boot/rom/prefs.c is the parser every NeoBench has booted with, and
 * libs/nb_rs/src/prefs.rs is the phase-1 module written to parse
 * exactly the way it does -- same keys, same clamps, same quirks, same
 * refusal to be upset by anything.  This test feeds both halves the
 * same bytes through the same door: pfs_find(), the one piece of
 * store the parser touches, is stood for by four strings the test
 * serves up (the way test_iso9660 stands in for hardware with two
 * fake devices), the C parses them into nb_prefs, and then
 * nb_rs_prefs_check() -- the very call kernel_init makes at boot --
 * parses them again and hands back a mask of the fields the two
 * disagreed on.  Anything but zero fails.
 *
 * Agreement alone would pass two parsers that were both wrong, so the
 * battery after it pins what the values must actually be: the clamps,
 * the defaults a bad value falls back to (sometimes the old value,
 * sometimes not -- width = 800 followed by width = naff is 640, and
 * bg_top = naff keeps the colour it had), the one-letter tells that
 * decide on/off, the ten-digit wrap of unsigned arithmetic, and the
 * four real Config files this machine ships with, read out of
 * system/Config the way the ROM reads them out of its store.
 *
 * The last two cases are the negative control: Rust is deliberately
 * shown a different file than the C, and the mask that comes back has
 * to name exactly the field that moved -- one bit, in struct order.
 * A check that cannot fail is not a check.
 *
 *   cc -O2 -Wall -Wextra -std=c99 -o test_prefs test_prefs.c \
 *       ../../boot/rom/prefs.c ../../libs/nb_rs/target/release/libnb_rs.a \
 *       -lpthread -ldl -lm
 *   ./test_prefs
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../boot/rom/prefs.h"
#include "../../boot/rom/pfs.h"

/* What prefs.c would find in gfx.c and amiga.c: which face the
 * preference selected, and the serial read-out of the values, are not
 * under test here. */
void gfx_font(int face)        { (void)face; }
void amiga_serial_putc(char c) { (void)c; }

/*
 * The host's view of the struct, and not the ROM's: an int aligns at
 * four bytes here and at two on m68k-linux-gnu, so everything after
 * `colour` lands two bytes later in the ROM (size 150 there, 152
 * here).  rustc asserts both against itself in prefs.rs, gcc's m68k
 * numbers against itself in kernel_init, and each pair meets where it
 * matters -- here, and at boot.
 */
_Static_assert(sizeof(struct nb_prefs) == 152, "the host's struct nb_prefs moved");
_Static_assert(offsetof(struct nb_prefs, font) == 44, "layout moved");
_Static_assert(offsetof(struct nb_prefs, colour) == 80, "layout moved");
_Static_assert(offsetof(struct nb_prefs, start_x) == 84, "layout moved");
_Static_assert(offsetof(struct nb_prefs, snd_file) == 100, "layout moved");
_Static_assert(offsetof(struct nb_prefs, hold) == 140, "layout moved");
_Static_assert(offsetof(struct nb_prefs, failsafe) == 148, "layout moved");

/* The Rust half, as the kernel declares it (kernel_init). */
extern unsigned nb_rs_prefs_check(const unsigned char **files,
                                  const unsigned *lens,
                                  const struct nb_prefs *have);
extern unsigned nb_rs_prefs_size(void);

extern unsigned nb_rs_selftest(unsigned seed);

/* ------------------------------------------------------------------ *
 * The store, stood for by four strings
 * ------------------------------------------------------------------ */

#define NFILES 4

static const char *const path_of[NFILES] = {
    "Config/screen.cfg", "Config/pointer.cfg",
    "Config/sound.cfg",  "Config/boot.cfg"
};

static struct pfs_node nodes[NFILES];
static int             present[NFILES];

const struct pfs_node *pfs_find(const char *path)
{
    int i;

    for (i = 0; i < NFILES; i++)
        if (present[i] && strcmp(path, path_of[i]) == 0)
            return &nodes[i];
    return NULL;
}

static void clear(void)
{
    memset(present, 0, sizeof(present));
    memset(nodes, 0, sizeof(nodes));
}

static void serve_n(int i, const char *text, unsigned len)
{
    nodes[i].name   = strrchr(path_of[i], '/') + 1;
    nodes[i].path   = path_of[i];
    nodes[i].parent = PFS_ROOT;
    nodes[i].data   = (const unsigned char *)text;
    nodes[i].size   = len;
    nodes[i].dir    = 0;
    present[i]      = 1;
}

static void serve(int i, const char *text)
{
    serve_n(i, text, (unsigned)strlen(text));
}

/* A directory wearing a file's name: not a preference file. */
static void serve_dir(int i, const char *text, unsigned len)
{
    serve_n(i, text, len);
    nodes[i].dir = 1;
}

/* ------------------------------------------------------------------ *
 * Both halves, one door
 * ------------------------------------------------------------------ */

/* C parses whatever is served, then Rust parses the same bytes and
 * its answer is held against C's -- exactly kernel_init's call. */
static unsigned agree(void)
{
    const unsigned char *files[NFILES];
    unsigned lens[NFILES];
    int i;

    nb_prefs_load();
    for (i = 0; i < NFILES; i++)
    {
        files[i] = present[i] && !nodes[i].dir ? nodes[i].data : NULL;
        lens[i]  = present[i] && !nodes[i].dir ? nodes[i].size : 0;
    }
    return nb_rs_prefs_check(files, lens, &nb_prefs);
}

static int failures;

static void ok(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Serve one file as screen.cfg, parse it both ways, and fail with
 * the mask and the text if the halves split. */
static void one(const char *text)
{
    unsigned m;

    clear();
    serve(0, text);
    m = agree();
    if (m)
    {
        printf("FAIL: halves disagree %08x on \"%s\"\n", m, text);
        failures++;
    }
}

/* gfx.h's NB_RGB, for the defaults (565 fields taken straight in) */
static uint16_t packed(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 31u) << 11) | ((g & 63u) << 5) | (b & 31u));
}

/* to_rgb()'s reduction, for #RRGGBB as a file writes it */
static uint16_t from888(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* The four files as they ship, read the way mkpfs packed them */
static char *slurp(const char *path, unsigned *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n)
    {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    b[n] = '\0';
    *len = (unsigned)n;
    return b;
}

int main(void)
{
    /* The struct itself: rustc's repr(C) and gcc's sizeof, one
     * number.  The static asserts above already pinned gcc's. */
    ok(nb_rs_prefs_size() == sizeof(struct nb_prefs),
       "rustc and gcc agree about struct nb_prefs");

    /* No files at all: both halves on the defaults prefs.c lays
     * down, word for word. */
    clear();
    ok(agree() == 0, "the defaults parse alike");
    ok(nb_prefs.w == 640 && nb_prefs.h == 512 && nb_prefs.depth == 8,
       "the raster defaults");
    ok(nb_prefs.lace == 1 && nb_prefs.grid == 1 && nb_prefs.glow == 1 &&
       nb_prefs.taskbar == 1, "the layer defaults");
    ok(nb_prefs.bar_style == 0 && nb_prefs.bar_glass == 20 &&
       nb_prefs.backdrop == 0, "the Vista bar and wash defaults");
    ok(nb_prefs.bg_top == packed(1, 7, 8) &&
       nb_prefs.bg_bot == packed(1, 19, 16) &&
       nb_prefs.colour == packed(31, 63, 31), "the colour defaults");
    ok(strcmp(nb_prefs.font, "Xen") == 0, "the face default");
    ok(nb_prefs.shape == 0 && nb_prefs.scale == 1 && nb_prefs.speed == 2 &&
       nb_prefs.shadow == 1 && nb_prefs.visible == 1 &&
       nb_prefs.start_x == 320 && nb_prefs.start_y == 256,
       "the pointer defaults");
    ok(nb_prefs.snd_startup == 1 && nb_prefs.snd_volume == 48 &&
       strcmp(nb_prefs.snd_file, "Core/Media/startup.snd") == 0,
       "the sound defaults");
    ok(nb_prefs.hold == 3 && nb_prefs.hwscan == 1 && nb_prefs.failsafe == 0,
       "the boot defaults");

    /* The clamps: what runs away is bounded, both halves the same
     * bound. */
    one("hold = 99\n");         ok(nb_prefs.hold == 15, "hold clamps at 15");
    one("hold = 0\n");          ok(nb_prefs.hold == 0, "hold can be silent");
    one("volume = 999\n");      ok(nb_prefs.snd_volume == 64, "volume clamps at 64");
    one("glass = 500\n");       ok(nb_prefs.bar_glass == 100, "glass clamps at 100");
    one("speed = 99\n");        ok(nb_prefs.speed == 8, "speed clamps at 8");
    one("speed = 0\n");         ok(nb_prefs.speed == 1, "speed floors at 1");
    one("scale = 9\n");         ok(nb_prefs.scale == 2, "scale is 1 or 2");

    /* Whose default a bad value falls back to: some keys hand out a
     * fresh one, some keep the value they had. */
    one("hold = banana\n");              ok(nb_prefs.hold == 3, "a bad hold is 3");
    one("hold = 7\nhold = banana\n");    ok(nb_prefs.hold == 3, "a bad hold falls to 3, not 7");
    one("width = 800\nwidth = naff\n");  ok(nb_prefs.w == 640, "a bad width falls to 640, not 800");
    one("width = 800\n");                ok(nb_prefs.w == 800, "a good width takes");
    one("volume = naff\n");              ok(nb_prefs.snd_volume == 48, "a bad volume is 48");
    one("glass = naff\n");               ok(nb_prefs.bar_glass == 20, "a bad glass is 20");
    one("bg_top = #ffffff\nbg_top = naff\n");
    ok(nb_prefs.bg_top == 0xffff, "a bad colour keeps the one before it");
    one("backdrop = azure\nbackdrop = porcelain\n");
    ok(nb_prefs.backdrop == 2, "an unknown backdrop keeps the choice");
    one("start = 100,200\nstart = naff\n");
    ok(nb_prefs.start_x == 100 && nb_prefs.start_y == 200,
       "a bad start keeps the park");

    /* One letter decides on and off, and each key keeps its own
     * default when the letter decides nothing. */
    one("grid = OFF\n");         ok(nb_prefs.grid == 0, "OFF is off");
    one("glow = True\n");        ok(nb_prefs.glow == 1, "True is on");
    one("taskbar = banana\n");   ok(nb_prefs.taskbar == 1, "an unreadable on/off keeps its default");
    one("shadow = 0\n");         ok(nb_prefs.shadow == 0, "0 is off");
    one("visible = f\n");        ok(nb_prefs.visible == 0, "f is off");
    one("startup = no\n");       ok(nb_prefs.snd_startup == 0, "no is off");
    one("scan = banana\n");      ok(nb_prefs.hwscan == 1, "scan defaults on");
    one("failsafe = banana\n");  ok(nb_prefs.failsafe == 0, "failsafe defaults off");
    one("failsafe = YES\n");     ok(nb_prefs.failsafe == 1, "YES is on");
    one("grid = on\n");          ok(nb_prefs.grid == 1, "on is on");
    one("grid = off\n");         ok(nb_prefs.grid == 0, "off is off");
    one("grid = o\n");           ok(nb_prefs.grid == 0, "a bare o is off");
    one("grid = orange\n");      ok(nb_prefs.grid == 0, "orange is off too");

    /* Names, read the way the C reads them: the flag in the mode's
     * name, any c in the bar's, case nowhere at all. */
    one("mode = hires-lace\n");  ok(nb_prefs.lace == 1, "lace in the name is lace");
    one("mode = hires\n");       ok(nb_prefs.lace == 0, "hires alone is not");
    one("mode = LACE\n");        ok(nb_prefs.lace == 1, "the name folds case");
    one("mode = banana\n");      ok(nb_prefs.lace == 0, "an unreadable mode is hires");
    one("bar = classic\n");      ok(nb_prefs.bar_style == 1, "classic is classic");
    one("bar = aero\n");         ok(nb_prefs.bar_style == 0, "aero is aero");
    one("bar = black\n");        ok(nb_prefs.bar_style == 1, "any c at all is classic");
    one("backdrop = SLATE\n");   ok(nb_prefs.backdrop == 4, "backdrops fold case");
    one("shape = ibeam\n");      ok(nb_prefs.shape == 2, "ibeam");
    one("shape = D\n");          ok(nb_prefs.shape == 3, "one capital letter");
    one("shape = garlic\n");     ok(nb_prefs.shape == 0, "anything else is the arrow");

    /* Colours: six hex digits in, five-six-five out, or no change. */
    one("bg_top = #ffffff\n");   ok(nb_prefs.bg_top == 0xffff, "#ffffff is white");
    one("bg_top = ffffff\n");    ok(nb_prefs.bg_top == 0xffff, "the hash is optional");
    one("bg_top = #0A1E45\n");
    ok(nb_prefs.bg_top == from888(10, 30, 69), "#0A1E45 reduces to 08e8");
    one("colour = #F4FAFF\n");
    ok(nb_prefs.colour == from888(244, 250, 255), "#F4FAFF reduces to f7df");
    one("bg_top = #zzzzzz\n");
    ok(nb_prefs.bg_top == packed(1, 7, 8), "a colour that is not one keeps the default");
    one("bg_top = #ff80\n");
    ok(nb_prefs.bg_top == packed(1, 7, 8), "five digits is not six");

    /* Pairs need the comma; each half keeps whatever half did not
     * convert. */
    one("start = 100,200\n");
    ok(nb_prefs.start_x == 100 && nb_prefs.start_y == 200, "the pair takes");
    one("start = 100\n");
    ok(nb_prefs.start_x == 320 && nb_prefs.start_y == 256, "no comma, no change");
    one("start = naff,44\n");
    ok(nb_prefs.start_x == 320 && nb_prefs.start_y == 44, "half a pair keeps the other half");
    one("start = 100,\n");
    ok(nb_prefs.start_x == 100 && nb_prefs.start_y == 256, "an empty half keeps its own");

    /* Lines that are not lines cost their own effect and nothing
     * else -- the whole of the parser's error handling. */
    one("# hold = 9\n");         ok(nb_prefs.hold == 3, "a comment is a comment");
    one("   # hold = 9\n");      ok(nb_prefs.hold == 3, "an indented one too");
    one("Hold = 9\n");           ok(nb_prefs.hold == 3, "keys are case-sensitive");
    one("holdx = 9\n");          ok(nb_prefs.hold == 3, "and exact");
    one("hold =\n");             ok(nb_prefs.hold == 3, "an empty value is not a value");
    one("= 9\n");                ok(nb_prefs.hold == 3, "no key, no effect");
    one("nonsense\n");           ok(nb_prefs.hold == 3, "no equals, no effect");
    one("hold = 5 # seconds\n"); ok(nb_prefs.hold == 5, "the count stops at the first non-digit");
    one("hold   =   5  \n");     ok(nb_prefs.hold == 5, "spaces are trimmed both sides");
    one("hold = 5\r\n");         ok(nb_prefs.hold == 5, "a CRLF file is a file");
    one("\n\nhold = 7\n\n");     ok(nb_prefs.hold == 7, "blank lines are skipped");
    one("hold = 9");             ok(nb_prefs.hold == 9, "no trailing newline");

    /* Ten digits of unsigned wrap, identically both sides. */
    one("width = 9999999999\n");
    ok(nb_prefs.w == 1410065407u, "ten nines wrap as unsigned does");

    /* Values cut where the C cuts them: 39 characters of value, 15
     * of face, 39 of path. */
    one("font = 0123456789012345678901234567890123456789\n");
    ok(strcmp(nb_prefs.font, "012345678901234") == 0, "the face stops at fifteen");
    one("file = 0123456789012345678901234567890123456789\n");
    ok(strcmp(nb_prefs.snd_file, "012345678901234567890123456789012345678") == 0,
       "the path stops at thirty-nine");

    /* A value ends where C's string ends: at its NUL. */
    {
        static const char withnul[] = { 'h', 'o', 'l', 'd', '=', '9', 0, 'x', 'y' };
        unsigned m;

        clear();
        serve_n(0, withnul, (unsigned)sizeof(withnul));
        m = agree();
        ok(m == 0, "a value with a NUL in it parses alike");
        ok(nb_prefs.hold == 9, "the value ends at its NUL");
    }

    /* Files stack: screen first, boot last, and a key set twice
     * belongs to whoever came later. */
    {
        unsigned m;

        clear();
        serve(0, "width = 800\nbg_top = #ffffff\n");
        serve(3, "width = 320\nhold = 6\n");
        m = agree();
        ok(m == 0, "two files parse alike");
        ok(nb_prefs.w == 320 && nb_prefs.hold == 6 && nb_prefs.bg_top == 0xffff,
           "the files stack in the order the kernel reads them");
    }

    /* A directory wearing a file's name is not a preference file,
     * on either side of the wire. */
    {
        unsigned m;

        clear();
        serve_dir(3, "hold = 9\n", 10);
        m = agree();
        ok(m == 0, "a directory parses alike");
        ok(nb_prefs.hold == 3, "a directory is not a preference file");
    }

    /* The four files this machine actually ships. */
    {
        static const char *const cfg[NFILES] = {
            "../../system/Config/screen.cfg", "../../system/Config/pointer.cfg",
            "../../system/Config/sound.cfg",  "../../system/Config/boot.cfg"
        };
        unsigned m;
        int i;

        clear();
        for (i = 0; i < NFILES; i++)
        {
            unsigned len;
            char *text = slurp(cfg[i], &len);

            if (!text)
            {
                printf("FAIL: cannot read %s\n", cfg[i]);
                failures++;
                continue;
            }
            serve_n(i, text, len);
        }
        m = agree();
        if (m)
        {
            printf("FAIL: the shipped Config files disagree %08x\n", m);
            failures++;
        }
        ok(nb_prefs.w == 640 && nb_prefs.h == 512 && nb_prefs.depth == 8 &&
           nb_prefs.lace == 1, "screen.cfg: the raster it names");
        ok(nb_prefs.bg_top == from888(10, 30, 69) &&
           nb_prefs.bg_bot == from888(10, 78, 134),
           "screen.cfg: the Vista wash it carries");
        ok(nb_prefs.backdrop == 0 && nb_prefs.grid == 1 && nb_prefs.glow == 1 &&
           nb_prefs.taskbar == 1 && nb_prefs.bar_style == 0 &&
           nb_prefs.bar_glass == 20 && strcmp(nb_prefs.font, "Xen") == 0,
           "screen.cfg: wash, layers, glass and face");
        ok(nb_prefs.shape == 0 && nb_prefs.scale == 1 && nb_prefs.speed == 2 &&
           nb_prefs.colour == from888(244, 250, 255) &&
           nb_prefs.shadow == 1 && nb_prefs.visible == 1 &&
           nb_prefs.start_x == 320 && nb_prefs.start_y == 256,
           "pointer.cfg: what it says");
        ok(nb_prefs.snd_startup == 1 && nb_prefs.snd_volume == 48 &&
           strcmp(nb_prefs.snd_file, "Core/Media/startup.snd") == 0,
           "sound.cfg: what it says");
        ok(nb_prefs.hold == 3 && nb_prefs.hwscan == 1 && nb_prefs.failsafe == 0,
           "boot.cfg: hold, scan and failsafe");
    }

    /* The negative control: Rust is shown a different file than the
     * C on purpose, and the mask has to name the one field that
     * moved.  Bit 24 is hold; bit 12 is font. */
    {
        static const char c_text[] = "hold = 5\n";
        static const char r_text[] = "hold = 9\n";
        const unsigned char *files[NFILES];
        unsigned lens[NFILES];

        memset(files, 0, sizeof(files));
        memset(lens, 0, sizeof(lens));
        clear();
        serve(0, c_text);
        nb_prefs_load();
        files[0] = (const unsigned char *)r_text;
        lens[0] = (unsigned)strlen(r_text);
        ok(nb_rs_prefs_check(files, lens, &nb_prefs) == (1u << 24),
           "the mask names hold alone when hold is what moved");
    }
    {
        static const char c_text[] = "font = Zap\n";
        static const char r_text[] = "font = Xen\n";
        const unsigned char *files[NFILES];
        unsigned lens[NFILES];

        memset(files, 0, sizeof(files));
        memset(lens, 0, sizeof(lens));
        clear();
        serve(0, c_text);
        nb_prefs_load();
        files[0] = (const unsigned char *)r_text;
        lens[0] = (unsigned)strlen(r_text);
        ok(nb_rs_prefs_check(files, lens, &nb_prefs) == (1u << 12),
           "the mask names font alone when font is what moved");
    }

    /* And the call itself, when there is nothing to hold against. */
    {
        const unsigned char *files[NFILES];
        unsigned lens[NFILES];

        memset(files, 0, sizeof(files));
        memset(lens, 0, sizeof(lens));
        ok(nb_rs_prefs_check(NULL, lens, &nb_prefs) == (1u << 31),
           "no files at all is its own answer");
        ok(nb_rs_prefs_check(files, lens, NULL) == (1u << 31),
           "no struct to hold against is its own answer");
    }

    /* The boot selftest, run here too: six legs over division,
     * multiplication, rotation, byte order and two counting loops.
     * Each leg keeps one side behind black_box precisely so a
     * compiler cannot prove the identity itself -- which it will try,
     * x/d*d + x%d being true whether or not any machine ever divides
     * -- and every divisor and trip count is stirred out of the seed.
     * On the host this catches a wrong identity; that the code is
     * there to run at all is checked on the m68k side by reading the
     * member's disassembly (the first build folded the whole
     * function to `return 0' and this would have sailed through). */
    ok(nb_rs_selftest(0x12345678u) == 0,
       "the boot selftest holds for one seed");
    ok(nb_rs_selftest(0x9e3779b9u) == 0,
       "and for another");

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_prefs: all checks passed\n");
    return 0;
}
