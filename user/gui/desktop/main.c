/*
 * nb_desktop_render() -- the NeoBench "futuristic clean" desktop.
 *
 * AGA, 640x512 interlaced, 64 colours (boot/rom/gfx.h).  Everything
 * below is composited into the RGB565 back buffer with the gfx
 * primitives and then quantised and packed by gfx_present(): there is no
 * bitmap anywhere in the ROM.
 *
 * Freestanding m68k rules apply throughout -- no libgcc and no floating
 * point.  Division is either by a constant the compiler folds into a
 * shift, or through the ROM's own lib32.c helpers (the RAM gauge divides
 * by the 136 MB NeoBench is built for); the numbers that have to move
 * without either -- the uptime -- are made by subtracting instead.
 *
 * The scene, top to bottom:
 *
 *   wallpaper  white -> mint vertical gradient with three soft mint
 *              glows and a 32 px hairline grid under them, and the
 *              NeoBench mark and wordmark laid over the top of it all
 *   taskbar    the panel, cut as Aero cuts the Windows 7 bar: a pane
 *              of tinted glass over the backdrop, lit along its top
 *              edge, carrying the mark-only start orb flush left, the
 *              pinned launchers, one task button per program that is
 *              running (the lit one is the one on top), the tray, the
 *              two-line clock and the show-desktop sliver on the very
 *              edge
 *
 * That is the whole scene the boot leaves on screen: backdrop and bar.
 * Nothing on the wallpaper at all -- the places the desktop used to
 * show as a column of icons down its left edge are entries in the
 * start menu the orb opens, Files leads them, and so are the directory
 * browser, the clock, the monitor, the about panel, Preferences and
 * NeoText.  Each
 * program is a flag set by that menu and tested in the draw pass below,
 * so what is on screen is exactly what the user asked for.
 * Config/screen.cfg carries "bar = aero | classic" (the flat Workbench
 * field the bar was drawn with first), "glass", how much of the
 * backdrop the pane shows, and "backdrop", which of the five washes the
 * desktop paints -- Preferences changes the same choice at once, but
 * only the file decides what the machine comes up with.
 */

#include "../../../boot/rom/gfx.h"
#include "../../../boot/rom/amiga.h"
#include "../../../boot/rom/kbd.h"
#include "../../../boot/rom/prefs.h"
#include "../../../boot/rom/pfs.h"
#include "../../../boot/rom/pdf.h"
#include "../../../boot/rom/pointer.h"
#include "../../../boot/rom/probe.h"
#include "logo.h"

/* ------------------------------------------------------------------ *
 * Palette
 * ------------------------------------------------------------------ */
/*
 * The desktop is set in Workbench's terms: a grey field with black type
 * on it, the Workbench blue for whatever is chosen or on top, and the
 * brand teal kept for the marks NeoBench draws rather than spent on the
 * chrome around them.
 */
#define C_INK     NB_RGB(31, 63, 31)     /* white                       */
#define C_TEXT    NB_RGB(4, 9, 5)        /* body type: near black       */
#define C_MUTE    NB_RGB(9, 18, 9)       /* secondary type: mid grey    */
#define C_ACC     NB_RGB(6, 41, 22)      /* brand teal                  */
#define C_ACC_D   NB_RGB(3, 22, 13)      /* dim teal                    */
#define C_GREEN   NB_RGB(6, 54, 8)       /* status green                */
#define C_SHADOW  NB_RGB(0, 1, 1)        /* drop shadow                 */

/* Workbench's chrome: #AAAAAA body, #0055AD for a chosen entry or the
 * title of the window on top, #595959 for a shaded bevel and #313131
 * for the hard edge a window, a menu and a gadget are drawn with. */
#define C_WB_GREY  NB_RGB(21, 42, 21)    /* #AAAAAA panel body          */
#define C_WB_BLUE  NB_RGB(0, 21, 21)     /* #0055AD chosen / active     */
#define C_WB_SHADE NB_RGB(11, 21, 11)    /* #595959 shaded bevel        */
#define C_WB_LINE  NB_RGB(6, 12, 6)      /* #313131 hard edge           */

/*
 * Aero's own glass, for the taskbar and the menu the orb opens.  None
 * of these is ever painted at full strength -- the bar is a near-black
 * blue-grey laid over the backdrop at an alpha, so the wash behind it
 * shows through and the whole field reads as tinted glass rather than
 * as a grey rectangle.  C_AERO is almost the colour of the glass
 * itself; C_AERO_HI is the band it closes off with; C_AERO_RIM is the
 * light steel every edge of it is cut with; C_AERO_BTN is the field a
 * launcher sits on and C_AERO_ACT the same field lit for the window
 * that is on top.  C_AERO_TXT is the type colour: light enough to
 * read on that glass, never white, because white belongs to the entry
 * that is chosen.
 */
#define C_AERO     NB_RGB(1, 3, 6)      /* the glass itself            */
#define C_AERO_HI  NB_RGB(4, 9, 14)     /* its lighter band            */
#define C_AERO_RIM NB_RGB(11, 26, 15)   /* the rim and the button edge */
#define C_AERO_BTN NB_RGB(7, 16, 12)    /* a pinned launcher's field   */
#define C_AERO_ACT NB_RGB(11, 25, 17)   /* the task button that is up  */
#define C_AERO_TXT NB_RGB(23, 49, 27)   /* type set on the glass       */

/* The older wash, still behind the grid and glow preferences. */
#define C_GLOW_A  NB_RGB(19, 53, 25)     /* mint horizon glow           */
#define C_GLOW_B  NB_RGB(20, 53, 28)     /* sky glow                    */
#define C_GLOW_C  NB_RGB(19, 56, 25)     /* aqua glow                   */
#define C_GRID    NB_RGB(21, 58, 28)     /* hairline grid               */

/*
 * The icon set is MUI's manner rather than the modern plate-and-mark:
 * a rounded tile whose body ramps from a saturated tint at the top to
 * a deep one at the foot, a white rim round it, an arc of light across
 * the top, and the mark cut out of the ramp in white.  The ramps stop
 * at about five to one against white because that is what the mark --
 * which straddles the whole tile -- has to read on at its lightest.
 *
 * Five hues, two stops each, and the same five serve the 24 pixel
 * place icons, the 14 pixel program glyphs, the 16 pixel launchers and
 * the 12 pixel browser rows, so one ramp pair is one colour of the set
 * at whatever size it is drawn.  The slate pair is the sixth: it is
 * what a plain file is drawn in, so a file never wears a hue that
 * means somewhere or something else.
 */
#define MUI_CUT   NB_RGB(1, 2, 6)        /* what a mark is cut out in    */

#define MUI_VIO_T NB_RGB(15, 14, 29)     /* #7C3AED violet, lit   Home   */
#define MUI_VIO_B NB_RGB(9, 7, 18)       /* #4C1D95 violet, deep         */
#define MUI_BLU_T NB_RGB(5, 24, 29)      /* #2563EB blue, lit      Core  */
#define MUI_BLU_B NB_RGB(4, 14, 17)      /* #1E3A8A blue, deep           */
#define MUI_GRN_T NB_RGB(3, 32, 7)       /* #15803D green, lit     Bench */
#define MUI_GRN_B NB_RGB(1, 11, 3)       /* #052E16 green, deep          */
#define MUI_AMB_T NB_RGB(22, 21, 1)      /* #B45309 amber, lit     Docs  */
#define MUI_AMB_B NB_RGB(15, 13, 2)      /* #78350F amber, deep          */
#define MUI_ROS_T NB_RGB(27, 7, 9)       /* #E11D48 rose, lit      Media */
#define MUI_ROS_B NB_RGB(17, 5, 7)       /* #881337 rose, deep           */
#define MUI_GRY_T NB_RGB(11, 29, 17)     /* #5B7488 slate, lit     file  */
#define MUI_GRY_B NB_RGB(4, 14, 9)       /* #24384A slate, deep          */

/* ------------------------------------------------------------------ *
 * Small helpers
 * ------------------------------------------------------------------ */

/* Decimal output without a printf -- and without libgcc. */
static char *put_num(char *d, unsigned v)
{
    static const unsigned decade[10] = {
        1000000000U, 100000000U, 10000000U, 1000000U, 100000U,
        10000U, 1000U, 100U, 10U, 1U
    };
    unsigned i;
    int started = 0;

    for (i = 0; i < 10; i++) {
        unsigned digit = 0;

        while (v >= decade[i]) {
            v -= decade[i];
            digit++;
        }
        if (digit || started || i == 9) {
            started = 1;
            *d++ = (char)('0' + digit);
        }
    }
    return d;
}

static char *put_str(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    return d;
}

/* the same, for the counters that can walk backwards */
static char *put_snum(char *d, int v)
{
    if (v < 0) {
        *d++ = '-';
        v = -v;
    }
    return put_num(d, (unsigned)v);
}

static void fmt_time(char *d, unsigned h, unsigned m, unsigned s)
{
    if (h)
        d = put_num(d, h);
    else
        *d++ = '0';
    *d++ = ':';
    if (m < 10)
        *d++ = '0';
    d = put_num(d, m);
    *d++ = ':';
    if (s < 10)
        *d++ = '0';
    d = put_num(d, s);
    *d = '\0';
}

static int strw(const char *s)
{
    int n = 0;

    while (s[n])
        n++;
    return n * 8;
}

/*
 * Type on the chrome: Workbench sets it plainly, no drop of its own,
 * because black on grey or white on blue does not need one and a drop
 * under type that small only thickens it.  Type on the wallpaper -- the
 * places in the start menu, which are set on plates -- keeps its own
 * flat colour instead.
 */
static void text_d(int x, int y, const char *s, uint16_t c)
{
    gfx_text(x, y, s, c);
}

static void text_right(int right, int y, const char *s, uint16_t c)
{
    text_d(right - strw(s), y, s, c);
}

/* ------------------------------------------------------------------ *
 * Wallpaper
 * ------------------------------------------------------------------ */

/*
 * The backdrop, and this one is NeoBench's own: the wash -- white
 * falling to mint by default -- with three soft discs laid over it,
 * then the hairline grid, and the mark and the wordmark on top of all
 * of it.  Nothing is sunk into anything: against a light field the
 * artwork carries itself, so it goes down last and at full strength.
 * The wordmark is navy where it used to be teal, because navy is what
 * still reads as type on white -- the teal behind it is only its
 * shadow now.
 *
 * Which wash it is, is one of the five the Preferences pane offers and
 * Config/screen.cfg names: wash is this white-to-mint one, and the
 * other four are a cream, a sky, a peach falling to lilac and a cool
 * grey-blue.  They all carry the same glows, the same grid and the
 * same artwork, which is why they are five hues of one field rather
 * than five different desktops.
 *
 * The Workbench this desktop takes its chrome from lays a plain blue
 * ramp behind everything; that is the one part of it that is not
 * borrowed, deliberately.  The wash, the glows and the grid all sit
 * behind the two preference flags below.
 *
 * A glow, when it is asked for, is five stacked passes of a fifth of
 * the strength, smallest last: the middle of the disc picks up all five
 * and the rim only the first, so it falls away in soft steps instead of
 * ending on an edge.
 */
static void glow(int cx, int cy, int r, uint16_t c, uint8_t a)
{
    int i;

    for (i = 5; i >= 1; i--)
        gfx_disc_a(cx, cy, (r * i) / 5, c, (uint8_t)(a / 5));
}

static void wallpaper(void)
{
    int i;
    uint16_t top, bot;

    nb_bd_colours(nb_prefs.backdrop, &top, &bot);
    gfx_vgrad(0, 0, 640, 512, top, bot);

    if (nb_prefs.glow)
    {
        glow(300, 452, 250, C_GLOW_A, 165);
        glow(556, 476, 210, C_GLOW_B, 175);
        glow(596, 58, 170, C_GLOW_C, 185);
    }

    if (nb_prefs.grid)
    {
        for (i = 0; i < 512; i += 32)
            gfx_alpha(0, i, 640, 1, C_GRID, 26);
        for (i = 16; i < 640; i += 32)
            gfx_alpha(i, 0, 1, 512, C_GRID, 15);
    }

    logo_mark(110, 210, 210);
    gfx_text_s(111, 429, "NEOBENCH", LOGO_TEAL, 3);   /* shadow, down-right */
    gfx_text_s(110, 428, "NEOBENCH", LOGO_NAVY, 3);   /* the wordmark       */
}

/* ------------------------------------------------------------------ *
 * Icons -- the MUI manner: a rounded tile that ramps from a saturated
 * tint to a deep one, a white rim, an arc of light along the top, and
 * the mark cut out of the ramp in white.  One primitive, mui_plate(),
 * draws the tile at every size the set is used at; every mark is then
 * positioned against the ramp, never against a flat plate, so all four
 * sizes read as one family.
 * ------------------------------------------------------------------ */

/*
 * A tile of the side s at (x, y) with corner radius r: a drop under
 * it, the white rim, the ramp inside the rim, then the highlight arc.
 * The arc keeps the rim's radius, which is what gives it the lens
 * shape a gel highlight has -- it curves away at both ends instead of
 * stopping square.  It is a quarter of the tile tall, except on the
 * small tiles, where a quarter would eat the band the mark is cut in,
 * so those two get a two pixel hairline instead.
 */
static void mui_plate(int x, int y, int s, int r,
                      uint16_t c_top, uint16_t c_bot)
{
    int gl = (s >= 16) ? s / 4 : 2;

    gfx_alpha_r(x + 1, y + 2, s, s, r + 1, C_SHADOW, 76);
    gfx_fill_r(x, y, s, s, r, C_INK);
    gfx_vgrad_r(x + 1, y + 1, s - 2, s - 2, r - 1, c_top, c_bot);
    gfx_alpha_r(x + 1, y + 1, s - 2, gl, r - 1, C_INK, 104);
}

/* the mark of a place, on the 24 pixel tile it is drawn on */

/* Home: a house -- white roof and walls, the doorway cut dark */
static void icon_home(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_VIO_T, MUI_VIO_B);
    gfx_tri(x + 4, y + 14, x + 12, y + 7, x + 20, y + 14, C_INK);
    gfx_fill(x + 6, y + 14, 12, 6, C_INK);
    gfx_fill(x + 10, y + 16, 4, 4, MUI_CUT);
}

/* Core: a monitor -- white bezel, the screen cut dark and lit with two
   lines of type, then the stand */
static void icon_screen(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_BLU_T, MUI_BLU_B);
    gfx_fill_r(x + 4, y + 7, 16, 11, 2, C_INK);
    gfx_fill_r(x + 5, y + 8, 14, 9, 1, MUI_CUT);
    gfx_fill(x + 7, y + 11, 8, 1, C_INK);
    gfx_fill(x + 7, y + 13, 5, 1, C_INK);
    gfx_fill(x + 11, y + 18, 2, 2, C_INK);
    gfx_fill(x + 9, y + 20, 6, 1, C_INK);
}

/* Bench: a rising bar chart standing on a baseline */
static void icon_bench(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_GRN_T, MUI_GRN_B);
    gfx_fill(x + 7, y + 16, 2, 3, C_INK);
    gfx_fill(x + 10, y + 13, 2, 6, C_INK);
    gfx_fill(x + 13, y + 10, 2, 9, C_INK);
    gfx_fill(x + 16, y + 8, 2, 11, C_INK);
    gfx_fill(x + 5, y + 19, 14, 1, C_INK);
}

/* Docs: a folder, tab and all, with the front flap shaded */
static void icon_docs(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_AMB_T, MUI_AMB_B);
    gfx_fill(x + 5, y + 8, 7, 3, C_INK);
    gfx_fill_r(x + 4, y + 10, 16, 10, 2, C_INK);
    gfx_alpha(x + 4, y + 15, 16, 5, MUI_CUT, 150);
}

/* Media: a play badge -- white disc with the triangle cut out of it */
static void icon_media(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_ROS_T, MUI_ROS_B);
    gfx_disc(x + 12, y + 14, 7, C_INK);
    gfx_tri(x + 10, y + 10, x + 10, y + 18, x + 18, y + 14, MUI_CUT);
}

/* Files: the cabinet itself -- two drawer fronts, each with its handle
   cut light across it, so the drawer that holds all the others reads as
   the drawer rather than as another folder beside them */
static void icon_files(int x, int y)
{
    mui_plate(x, y, 24, 6, MUI_BLU_T, MUI_BLU_B);
    gfx_fill_r(x + 4, y + 7, 16, 8, 2, C_INK);
    gfx_fill(x + 9, y + 10, 6, 2, MUI_CUT);
    gfx_fill(x + 4, y + 17, 16, 4, C_INK);
    gfx_fill(x + 9, y + 18, 6, 2, MUI_CUT);
}

/*
 * The program glyphs of the start menu -- the same tile at 14 pixels
 * with a mark that fits the eight rows the hairline leaves clear.
 * Files shares Core's blue, the clock takes Docs' amber, the monitor
 * Bench's green, About Home's violet and Preferences Media's rose, so
 * every hue in the set is the same hue wherever it turns up and none of
 * them is invented for a single place.  NeoText takes the slate that is
 * neither: it is the colour a plain file wears in the browser, and the
 * reader that opens plain files wears it too.
 */
static void menu_glyph(int i, int x, int y)
{
    switch (i)
    {
    case 0:                                          /* Files  */
        mui_plate(x, y, 14, 4, MUI_BLU_T, MUI_BLU_B);
        gfx_fill(x + 3, y + 4, 5, 2, C_INK);         /* tab    */
        gfx_fill_r(x + 3, y + 6, 9, 6, 1, C_INK);    /* body   */
        gfx_alpha(x + 3, y + 9, 9, 3, MUI_CUT, 150); /* flap   */
        break;
    case 1:                                          /* Clock  */
        mui_plate(x, y, 14, 4, MUI_AMB_T, MUI_AMB_B);
        gfx_disc(x + 7, y + 7, 4, C_INK);
        gfx_fill(x + 7, y + 5, 1, 3, MUI_CUT);       /* hand   */
        gfx_fill(x + 7, y + 7, 3, 1, MUI_CUT);
        break;
    case 2:                                          /* Monitor */
        mui_plate(x, y, 14, 4, MUI_GRN_T, MUI_GRN_B);
        gfx_fill_r(x + 2, y + 4, 10, 6, 1, C_INK);
        gfx_fill(x + 3, y + 5, 8, 4, MUI_CUT);
        gfx_fill(x + 6, y + 10, 2, 1, C_INK);
        gfx_fill(x + 4, y + 11, 6, 1, C_INK);
        break;
    case 3:                                          /* About  */
        mui_plate(x, y, 14, 4, MUI_VIO_T, MUI_VIO_B);
        gfx_disc(x + 7, y + 7, 4, C_INK);
        gfx_fill(x + 7, y + 4, 1, 1, MUI_CUT);       /* the i  */
        gfx_fill(x + 7, y + 6, 1, 3, MUI_CUT);
        break;
    case 4:                                          /* Preferences */
        /* two sliders, one above the other: the marks the pane itself
         * draws, so the glyph is the program at a size it fits in */
        mui_plate(x, y, 14, 4, MUI_ROS_T, MUI_ROS_B);
        gfx_fill(x + 3, y + 6, 8, 1, C_INK);
        gfx_fill(x + 3, y + 10, 8, 1, C_INK);
        gfx_fill(x + 5, y + 4, 2, 5, C_INK);         /* knobs */
        gfx_fill(x + 8, y + 8, 2, 5, C_INK);
        break;
    default:                                         /* NeoText */
        mui_plate(x, y, 14, 4, MUI_GRY_T, MUI_GRY_B);
        gfx_fill_r(x + 3, y + 4, 8, 8, 1, C_INK);    /* the sheet */
        gfx_fill(x + 4, y + 6, 6, 1, MUI_CUT);       /* three lines */
        gfx_fill(x + 4, y + 8, 6, 1, MUI_CUT);
        gfx_fill(x + 4, y + 10, 4, 1, MUI_CUT);
        break;
    }
}

/*
 * The rows of the file browser: the same tile at 12 pixels, the
 * smallest the set is drawn at, so the marks are kept to the eight
 * rows the hairline leaves.  Going up a level wears violet because it
 * is an action rather than a place, a directory wears Core's blue and
 * an ordinary file wears the slate that means neither.
 */
static void row_glyph(int kind, int x, int y)
{
    if (kind == 0)                                  /* up one level   */
    {
        mui_plate(x, y, 12, 3, MUI_VIO_T, MUI_VIO_B);
        gfx_tri(x + 3, y + 3, x + 7, y + 3, x + 5, y + 7, C_INK);
        gfx_fill(x + 4, y + 6, 3, 5, C_INK);
    }
    else if (kind == 1)                             /* a directory     */
    {
        mui_plate(x, y, 12, 3, MUI_BLU_T, MUI_BLU_B);
        gfx_fill(x + 2, y + 3, 4, 2, C_INK);
        gfx_fill_r(x + 2, y + 5, 7, 6, 1, C_INK);
        gfx_alpha(x + 2, y + 8, 7, 3, MUI_CUT, 150);
    }
    else                                            /* a file          */
    {
        mui_plate(x, y, 12, 3, MUI_GRY_T, MUI_GRY_B);
        gfx_fill_r(x + 2, y + 3, 7, 8, 1, C_INK);
        gfx_fill(x + 3, y + 5, 5, 1, MUI_CUT);
        gfx_fill(x + 3, y + 7, 5, 1, MUI_CUT);
    }
}

/*
 * Where the desktop's places went
 *
 * There used to be five icons down the left edge for these; they are
 * entries in the start menu the orb opens now, so the wallpaper is all
 * the desktop itself carries.  Each is still a path in the store rather
 * than something the shell knows about, so an entry can name a
 * directory two levels down without the desktop having to know the
 * tree -- and a place that is renamed is one string here and in
 * system/, never a hard-coded walk.
 *
 * Files leads them.  It is the drawer all the others are looked for in,
 * so it belongs with them rather than in the program list below the
 * rule, and an empty path is the one place none of the rest named: the
 * root of the store, which is where the browser starts.  Being a place
 * rather than a program it opens instead of toggling, and the cross is
 * what puts it away.
 */
#define N_PLACES 6

static const struct {
    const char *name;       /* what the entry says                     */
    const char *dir;        /* store directory it opens, "" = root     */
} places[N_PLACES] = {
    { "Files", ""           },        /* Files  */
    { "Home",  "Home"        },        /* Home   */
    { "Core",  "Core"        },        /* Core   */
    { "Bench", "Core/Bench"  },        /* Bench  */
    { "Docs",  "Core/Docs"   },        /* Docs   */
    { "Media", "Core/Media"  },        /* Media  */
};

/* the plate and mark of a place, at whatever size the caller asks for */
static void place_icon(int i, int x, int y)
{
    switch (i)
    {
    case 0:  icon_files(x, y);  break;
    case 1:  icon_home(x, y);   break;
    case 2:  icon_screen(x, y); break;
    case 3:  icon_bench(x, y);  break;
    case 4:  icon_docs(x, y);   break;
    default: icon_media(x, y);  break;
    }
}

/*
 * Choosing: one press names an item, the second one runs it
 *
 * Nothing opens on a single press any more.  The first press lights the
 * item up straight away -- that is the machine answering -- and the
 * second press, inside half a second and within twenty-four pixels, is
 * the one that opens it.  A press on the wallpaper, on the gap between
 * rows or on the wrong half of an icon then does exactly what it looks
 * like it does: nothing.
 *
 * Closing is still one press.  A cross, a task button and the orb are
 * controls, not items, and nobody should have to double click one.
 */
#define SEL_NONE  0
#define SEL_MENU  1
#define SEL_ROW   2

#define DBL_FIELDS  25        /* 500 ms -- fifty fields to the second  */
#define DBL_SLOP    24        /* pixels of drift between the presses   */

static int      sel_kind;     /* what the last press picked, SEL_NONE  */
static int      sel_idx;
static int      sel_x, sel_y; /* where that press landed               */
static unsigned sel_t;        /* nb_fields when it landed              */

static int near(int a, int b, int slop)
{
    int d = a - b;

    if (d < 0)
        d = -d;
    return d <= slop;
}

/*
 * The task manager's layout, shared by the draw pass and the click
 * pass so a button can never be drawn in one place and hit in another.
 * It is declared here because the click pass asks for it; the panel it
 * belongs to is further down the file.
 */
struct task_cell {
    int         x, w;          /* where its button sits, and how wide  */
    const char *name;          /* what the button says                 */
};
static int task_slot(int id, struct task_cell *t);

/* ------------------------------------------------------------------ *
 * Windows
 * ------------------------------------------------------------------ */

/*
 * Workbench's frame, which every panel on the desktop is drawn with: a
 * hard black edge all round, a lit edge inside it along the top and the
 * left, a shaded one along the bottom and the right, and the grey body
 * between them.  No glass and no blur: a Workbench window is opaque, so
 * there is nothing behind the frame to see through and no shadow to
 * throw onto the backdrop.
 */
static void wb_frame(int x, int y, int w, int h)
{
    gfx_fill(x, y, w, h, C_WB_GREY);

    gfx_fill(x, y, w, 1, C_WB_LINE);
    gfx_fill(x, y + h - 1, w, 1, C_WB_LINE);
    gfx_fill(x, y, 1, h, C_WB_LINE);
    gfx_fill(x + w - 1, y, 1, h, C_WB_LINE);

    gfx_fill(x + 1, y + 1, w - 2, 1, C_INK);
    gfx_fill(x + 1, y + 1, 1, h - 2, C_INK);
    gfx_fill(x + 1, y + h - 2, w - 2, 1, C_WB_SHADE);
    gfx_fill(x + w - 2, y + 1, 1, h - 2, C_WB_SHADE);
}

/*
 * A caption button, in Aero's terms rather than Workbench's: a rounded
 * cell of glass with a light steel rim and a line of light along its
 * own top edge, and the glyph cut into it in the type colour the glass
 * is set in.  It stands on the caption's glass, so it is laid down as
 * alphas -- there is no colour of its own to cover, only the wash the
 * window is standing on, which is what keeps it part of the pane
 * instead of a grey plate stuck to one.
 *
 * The cells are 13 wide on a pitch of 15 and the right-most is close,
 * which is the geometry on_close() reads back; the glyphs are the
 * Workbench ones, because a floor bar and a cross say minimise and
 * close on any desktop and a new symbol would only have to be learned.
 */
static void cap_btn(int x, int y, int kind)
{
    gfx_alpha_r(x, y, 13, 11, 3, C_AERO_RIM, 205);
    gfx_alpha_r(x + 1, y + 1, 11, 9, 2, C_AERO_BTN, 225);
    gfx_alpha(x + 2, y + 1, 9, 1, C_INK, 92);

    if (kind == 2) {
        gfx_line(x + 4, y + 3, x + 9, y + 8, C_AERO_TXT);
        gfx_line(x + 4, y + 8, x + 9, y + 3, C_AERO_TXT);
    } else if (kind == 1) {
        gfx_fill(x + 4, y + 3, 6, 6, C_AERO_TXT);
    } else {
        gfx_fill(x + 4, y + 7, 6, 2, C_AERO_TXT);
    }
}

/*
 * An Aero window.
 *
 * A pane of glass across the caption, a light steel rim round the
 * frame, a shadow thrown onto the backdrop below and to the right of
 * it, and Workbench's grey body and white field inside -- the bar and
 * the start menu are already drawn in these terms, and a window that
 * disagreed with them would read as a foreign object on the same
 * desktop.
 *
 * The caption is laid over the scene as the scene stands: nothing is
 * painted into that strip before the glass is, so the alpha composites
 * with the wash the wallpaper is carrying, or with the window this one
 * overlaps, and the caption carries what is behind it rather than
 * hiding it under a colour that only looks like glass.  The body below
 * is opaque, as a body has to be -- there is a program's own drawing
 * going on in it.
 *
 * The rim is square along its runs and stepped at the corners: one
 * pixel of steel turning the corner in two steps reads as round at
 * this size, and it needs no primitive that would fill the middle of
 * the frame and take the transparency with it.
 */
static void glass_window(int x, int y, int w, int h, int th,
                         const char *title, int nbtn)
{
    int i, bx;

    /* the shadow, before the frame so it never lands on the frame: a
     * falloff down and to the right, the way the light falls on the
     * rest of the chrome */
    for (i = 0; i < 4; i++)
        gfx_alpha(x + 3, y + h + i, w, 1, C_SHADOW, 96 - i * 24);
    for (i = 0; i < 4; i++)
        gfx_alpha(x + w + i, y + 4, 1, h - 3, C_SHADOW, 96 - i * 24);

    /* the caption's glass, over whatever the scene had put there */
    gfx_alpha(x + 1, y + 1, w - 2, th - 1, C_AERO, 200);

    /* the body, opaque, from where the caption stops */
    gfx_fill(x, y + th, w, h - th, C_WB_GREY);

    /* the rim: light steel along the runs, stepped at the four corners,
     * with the corner pixels themselves left to the backdrop because
     * that is what makes the corner read as cut away rather than filled */
    gfx_fill(x + 3, y, w - 6, 1, C_AERO_RIM);
    gfx_fill(x + 3, y + h - 1, w - 6, 1, C_AERO_RIM);
    gfx_fill(x, y + 3, 1, h - 6, C_AERO_RIM);
    gfx_fill(x + w - 1, y + 3, 1, h - 6, C_AERO_RIM);

    gfx_fill(x + 1, y, 2, 1, C_AERO_RIM);
    gfx_fill(x, y + 1, 1, 2, C_AERO_RIM);
    gfx_fill(x + 1, y + h - 1, 2, 1, C_AERO_RIM);
    gfx_fill(x, y + h - 3, 1, 2, C_AERO_RIM);
    gfx_fill(x + w - 3, y, 2, 1, C_AERO_RIM);
    gfx_fill(x + w - 2, y + 1, 1, 2, C_AERO_RIM);
    gfx_fill(x + w - 3, y + h - 1, 2, 1, C_AERO_RIM);
    gfx_fill(x + w - 2, y + h - 3, 1, 2, C_AERO_RIM);

    /* what every glass panel has: a lit line along its top edge, and a
     * closing bead where the pane meets the body below it */
    gfx_alpha(x + 3, y + 1, w - 6, 1, C_INK, 150);
    gfx_alpha(x + 3, y + 2, w - 6, 1, C_INK, 62);
    gfx_alpha(x + 1, y + th - 2, w - 2, 1, C_AERO_RIM, 96);
    gfx_alpha(x + 1, y + th - 1, w - 2, 1, C_SHADOW, 128);

    text_d(x + 14, y + (th - 8) / 2, title, C_AERO_TXT);

    bx = x + w - 8 - nbtn * 15;
    for (i = 0; i < nbtn; i++)
        cap_btn(bx + i * 15, y + (th - 11) / 2,
                (i == nbtn - 1) ? 2 : i);

    /* content pane */
    gfx_fill(x + 4, y + th + 4, w - 8, h - th - 8, C_INK);
    gfx_fill(x + 4, y + th + 4, w - 8, 1, C_WB_SHADE);
    gfx_fill(x + 4, y + th + 4, 1, h - th - 8, C_WB_SHADE);
    gfx_fill(x + 4, y + h - 5, w - 8, 1, C_WB_GREY);
    gfx_fill(x + w - 5, y + th + 4, 1, h - th - 8, C_WB_GREY);
}

/* a plain panel, for the gadgets and for the start menu */
static void panel(int x, int y, int w, int h, const char *title)
{
    wb_frame(x, y, w, h);

    if (title) {
        text_d(x + 8, y + 6, title, C_WB_LINE);
        gfx_fill(x + 2, y + 20, w - 4, 1, C_WB_LINE);
    }
}

/*
 * The about panel, and where it stands: the position the layout gave
 * it, plus whatever a drag has added since.  The offset is a .bss
 * variable, so it starts at zero without anything having to write to
 * the ROM image to make that true, and the base stays a constant.
 */
static int about_dx, about_dy;

#define AB_X        (124 + about_dx)
#define AB_Y        (40 + about_dy)
#define AB_W        300
#define AB_H        100

static void window_main(void)
{
    const int x = AB_X, y = AB_Y, w = AB_W, h = AB_H;

    glass_window(x, y, w, h, 20, "NeoBench", 3);

    text_d(x + 10, y + 28, "NeoBench 0.1.7", C_TEXT);
    text_d(x + 10, y + 42, "Futuristic desktop on AGA", C_MUTE);
    text_d(x + 10, y + 56, "(c) lord_protector 2026 & MiMo", C_MUTE);
    text_d(x + 10, y + 70, "060 AGA/RTG only (A1200/T A4000/T)", C_MUTE);
}

/* right-aligned column helper */
static void col_r(int right, int y, const char *s)
{
    text_d(right - strw(s), y, s, C_MUTE);
}

/*
 * Files -- the directory browser, reading straight out of the store the
 * ROM is built with (tools/mkpfs.py packs system/ into the image, so
 * this window browses what the boot actually loaded its preferences
 * from rather than a list of names).
 *
 * The geometry is #defines rather than locals because the click pass
 * below has to land on exactly the rows the draw pass put down.  Where
 * the window *stands* is the layout's position plus an offset the user
 * can drag it to; the offset is a .bss variable and so starts at zero,
 * which is what lets the base stay a constant in the ROM image.
 */
static int files_dx, files_dy;

#define FILES_X     (376 + files_dx)
#define FILES_Y     (176 + files_dy)
#define FILES_W     248
#define FILES_H     254
#define FILES_HEAD  28         /* "Name"/"Size" header row             */
#define FILES_ROW0  50         /* first list row, from the window top  */
#define FILES_ROWH  22         /* row pitch                            */
#define FILES_ROWS  7          /* ".." plus six children: the root has
                                * Apps Config Core Home Temp Tools, and
                                * the deepest drawer is one shorter     */

static unsigned cur_dir;       /* the directory being shown; 0 = root  */

/*
 * The other two program windows, and where they stand.  Preferences is
 * a small pane tucked beside the browser; NeoText takes the wide middle
 * of the screen, because a document needs the width.  They overlap each
 * other and the browser -- two windows in the space there is -- so the
 * draw pass paints the one with the keyboard last and the click pass
 * asks for it first, which is one z order stated twice.
 */
static int prefs_dx, prefs_dy;

#define PR_X        (110 + prefs_dx)
#define PR_Y        (150 + prefs_dy)
#define PR_W        250
#define PR_H        180
#define PR_S0       10          /* first swatch, from the window's edge */
#define PR_SY       42          /* and its row                           */
#define PR_SW       40          /* a swatch is 40 wide, 44 with its gap  */
#define PR_SH       26
#define PR_LAY      108         /* the layer check boxes, from the edge  */

static int text_dx, text_dy;

#define NT_X        (60 + text_dx)
#define NT_Y        (150 + text_dy)
#define NT_W        470
#define NT_H        256
#define NT_TX       (NT_X + 8)          /* the text's left edge          */
#define NT_TY       (NT_Y + 30)         /* the first row's top           */
#define NT_ROWS     22                  /* rows the pane stands          */
#define NT_COLS     55                  /* and how wide they are         */
#define NT_SB       (NT_X + NT_W - 17)  /* the scroll column             */
#define NT_SBH      (NT_ROWS * 9)       /* and how far it runs           */
#define NT_SBT      12                  /* an arrow is this tall         */
#define NT_SBY      (NT_Y + 26)         /* where the column starts       */
#define NT_SBTR     (NT_SBH - 2 * NT_SBT)   /* the groove between them   */
#define NT_SBMID    (NT_SBY + NT_SBT + NT_SBTR / 2)

/*
 * The two gadgets in the corner, laid out the same way as the windows:
 * a base that is a constant in ROM and an offset that a drag owns.  The
 * clock's base is its centre rather than its corner, because that is
 * how a dial is drawn -- the box the drag pass asks for is the square
 * that centre stands in, and it is put back the same way round.
 */
static int clock_dx, clock_dy;

#define CLOCK_X     (590 + clock_dx)
#define CLOCK_Y     (76 + clock_dy)
#define CLOCK_R     30

static int mon_dx, mon_dy;

#define MON_X       (452 + mon_dx)
#define MON_Y       (116 + mon_dy)
#define MON_W       172
#define MON_H       48

/* ------------------------------------------------------------------ *
 * Start menu: geometry and state, shared by the draw and click passes
 * ------------------------------------------------------------------ *
 *
 * The panel stands directly above the orb that opens it and stops
 * clear of the bar at y=490.  It carries two sections: the places,
 * which are the directory entries the desktop used to show as a column
 * of icons down its left edge and which take a full 24 pixel plate
 * each, and the programs, which are gadgets with a running dot on
 * them.  Both are addressed as one list, places first, because that is
 * how the selection, the click pass and the band all name an entry;
 * where the split falls is a matter of two constants, so the draw and
 * the hit area cannot drift apart.
 *
 * The flags below are the whole of what is running; they are file scope
 * without an initialiser because .data lands in write-only ROM.
 */
#define MENU_X      8
#define MENU_Y      112
#define MENU_W      196
#define MENU_H      238          /* MENU_Y + this stops short of the bar */
#define MENU_HEAD   22           /* entries start below the title rule   */
#define MENU_ITEMW  (MENU_W - 12)
#define MENU_PH     26           /* place row pitch: a 24 px plate, +1   */
#define MENU_ITEMH  34           /* program row pitch: 30 px box, 4 gap  */
#define N_PROGRAMS  6            /* programs the desktop can run         */
#define MENU_PROGS  1            /* of which the menu shows this many    */
#define MENU_ITEMS  (N_PLACES + MENU_PROGS)

#define MENU_P0     (MENU_Y + MENU_HEAD + 4)             /* first place */
#define MENU_SEP    (MENU_P0 + N_PLACES * MENU_PH + 4)   /* section rule */
#define MENU_G0     (MENU_SEP + 4)                       /* first program */

/*
 * Which program each row of the menu's program section starts.  The
 * numbers are the slots the whole desktop counts in -- the panel, the
 * task row and the keyboard's focus all go 1..6 in this order -- while
 * the menu lists one of the six.  Files leads the drawers above the
 * rule rather than sitting in this list, and Preferences, Clock,
 * Monitor and NeoText are filed in the store instead: the pane in
 * Config/ beside the files it reads, the other three in Tools/
 * beside the note that says what the drawer holds.  About keeps its
 * row, as the one program the menu carries for its own sake rather
 * than for the files around it.  All six are still programs, and all
 * six still take their buttons on the bar when they are running.
 */
static const unsigned char menu_slot[MENU_PROGS] = { 3 };

/* the show-desktop sliver: the last eight columns of the panel, which
 * is the width Aero gives it -- a strip you can find without looking,
 * not a gadget you have to aim at */
#define DESK_X      632

/* the task manager's row: after the launcher's rule, before the tray's */
#define TASK_X0     158
#define TASK_X1     436

static int menu_open;           /* the start menu is showing           */
static int menu_sticky;         /* right-clicked open: it stays up     */
static int files_open;          /* the directory browser window        */
static int clock_open;          /* the analog dial gadget              */
static int monitor_open;        /* the two-bar system monitor          */
static int about_open;          /* the NeoBench information panel      */
static int prefs_open;          /* the Preferences pane                */
static int pr_lay;              /* which of its rows the keys work on:
                                 * 0 the five swatches, 1 grid, 2 glow  */
static int neotext_open;        /* the text and document reader        */
static int focus_p;             /* the program the panel shows as
                                 * active: 0 none, else its menu slot  */
static int desk_hidden;         /* show-desktop has the windows down   */
static int desk_saved;          /* which ones were up when it did      */

/* ------------------------------------------------------------------ *
 * The rows a repaint has to touch
 *
 * Rebuilding the scene is cheap; presenting it used to be the whole
 * screen no matter what moved, because both the scene pass and the pack
 * walked all 512 rows.  The back buffer keeps whatever was not
 * overwritten, and it is correct there -- the scene is a pure function
 * of the flags -- so a click only ever owes the rows it changed.
 *
 * Every handler below registers its own rows and render() takes them a
 * band at a time.  Bands merge as they arrive; the fourth disjoint one
 * collapses the lot to the whole raster, which is always right and only
 * ever slower.  An empty set -- nothing has clicked yet, which is the
 * state .bss starts in -- means everything.
 * ------------------------------------------------------------------ */
#define BAND_MAX 4

static int band_a[BAND_MAX];            /* rows in flight, sorted, joined */
static int band_b[BAND_MAX];
static int band_n;

static void band_add(int y0, int y1)
{
    int i, j;

    if (y0 < 0) y0 = 0;
    if (y1 > 512) y1 = 512;
    if (y1 <= y0)
        return;

    for (i = 0; i < band_n; i++)
    {
        if (y1 < band_a[i])
            break;                      /* wholly above what is there    */
        if (y0 > band_b[i])
            continue;                   /* wholly below it               */

        /* overlapping or touching: absorb band i, then look again */
        y0 = (y0 < band_a[i]) ? y0 : band_a[i];
        y1 = (y1 > band_b[i]) ? y1 : band_b[i];
        for (j = i; j + 1 < band_n; j++)
        {
            band_a[j] = band_a[j + 1];
            band_b[j] = band_b[j + 1];
        }
        band_n--;
        i--;
    }

    if (band_n >= BAND_MAX)
    {
        band_a[0] = 0;
        band_b[0] = 512;
        band_n = 1;
        return;
    }

    for (i = 0; i < band_n; i++)
        if (y0 < band_a[i])
            break;
    for (j = band_n; j > i; j--)
    {
        band_a[j] = band_a[j - 1];
        band_b[j] = band_b[j - 1];
    }
    band_a[i] = y0;
    band_b[i] = y1;
    band_n++;
}

/* ------------------------------------------------------------------ *
 * Row extents of the gadgets, so a click can claim exactly what its
 * change will overwrite -- the panel, its drop shadow and the rim the
 * primitives put round it, never a row more.
 * ------------------------------------------------------------------ */
static void band_menu(void)     { band_add(MENU_Y - 4, 500); }
static void band_about(void)    { band_add(AB_Y - 4, AB_Y + AB_H + 14); }
static void band_clock(void)    { band_add(CLOCK_Y - CLOCK_R - 2,
                                           CLOCK_Y + CLOCK_R + 10); }
static void band_monitor(void)  { band_add(MON_Y - 4, MON_Y + MON_H + 13); }

/* The browser window, and -- when its task button comes or goes with
 * it -- the slice of the bar that carries that button. */
static void band_files(int bar)
{
    band_add(FILES_Y - 4, FILES_Y + FILES_H + 14);
    if (bar)
        band_add(486, 512);
}

/* The same two, for the pane and the reader.  Their bars say the same
 * thing theirs do: a window that has just appeared has just put a
 * button on the bar, and one that has gone has just left a gap. */
static void band_prefs(int bar)
{
    band_add(PR_Y - 4, PR_Y + PR_H + 14);
    if (bar)
        band_add(486, 512);
}

static void band_neotext(int bar)
{
    band_add(NT_Y - 4, NT_Y + NT_H + 14);
    if (bar)
        band_add(486, 512);
}

/*
 * Every program window at once, for the one control that takes them
 * all down together, plus the buttons their closes and opens leave
 * behind on the bar.  It is the whole raster rather than the slice the
 * windows used to be laid out in: a window can be dragged to any row
 * now, and a "show desktop" that left one of them standing would not
 * be showing the desktop.  The start menu is drawn over that range
 * too, and its entries carry the running dot, so it is claimed as well
 * whenever it is up.
 */
static void band_programs(void)
{
    band_add(0, 512);
    if (menu_open)
        band_menu();
}

/* The rows an item's light of selection covers: a menu entry -- a place
 * or a program, which are two different pitches -- or the one line of
 * the browser's list. */
static void band_select(int kind, int idx)
{
    if (kind == SEL_MENU)
    {
        if (idx < N_PLACES)
        {
            int y = MENU_P0 + idx * MENU_PH;

            band_add(y - 3, y + MENU_PH + 3);
        }
        else
        {
            int y = MENU_G0 + (idx - N_PLACES) * MENU_ITEMH;

            band_add(y - 3, y + 33);
        }
    }
    else if (kind == SEL_ROW)
    {
        int ry = FILES_Y + FILES_ROW0 + idx * FILES_ROWH;

        band_add(ry - 3, ry + FILES_ROWH + 3);
    }
}

/* The close button of a three-button glass caption: the right-most
 * 13x11 cell, at the offset the draw pass put it down at. */
static int on_close(int mx, int my, int wx, int wy, int ww)
{
    return mx >= wx + ww - 23 && mx < wx + ww - 10 &&
           my >= wy + 4 && my < wy + 15;
}

/*
 * The part of a caption a press can take hold of: the strip itself,
 * from the window's left edge to where the three buttons begin.  A
 * press here starts a move instead of naming anything, which is the
 * whole difference between a window's furniture and its contents -- the
 * buttons and the title are the frame, everything under the title is
 * what the program put there.
 */
static int on_caption(int mx, int my, int wx, int wy, int ww, int nbtn)
{
    int bx = wx + ww - 8 - nbtn * 15;

    return my >= wy && my < wy + 20 && mx >= wx && mx < bx;
}

/* Which directory an icon opens; pfs_find() cannot fail here because
 * the icons were picked to match the tree the ROM is built with. */
static unsigned dir_of(const char *path)
{
    const struct pfs_node *n = pfs_find(path);

    return n ? (unsigned)(n - nb_pfs_nodes) : PFS_ROOT;
}

/* The window caption doubles as the path read-out. */
static void path_title(char *buf, unsigned cap)
{
    static const char tag[] = "NeoBench:";
    const char *p = nb_pfs_nodes[cur_dir].path;
    unsigned i = 0;

    while (i + 1 < cap && tag[i])
    {
        buf[i] = tag[i];
        i++;
    }
    while (i + 1 < cap && *p)
    {
        buf[i] = *p;
        i++;
        p++;
    }
    if (i + 1 < cap && nb_pfs_nodes[cur_dir].path[0])
        buf[i++] = '/';
    buf[i] = '\0';
}

static void fmt_size(char *buf, unsigned bytes)
{
    char *d = buf;

    if (bytes >= 1024u)
    {
        d = put_num(d, (bytes + 512u) / 1024u);
        d = put_str(d, " KB");
    }
    else
    {
        d = put_num(d, bytes);
        d = put_str(d, " B");
    }
    *d = '\0';
}

/* The node row i leads to; row 0 is always the level above. */
static unsigned row_target(int i)
{
    unsigned c;
    int n = 1;

    if (i <= 0)
        return (cur_dir == PFS_ROOT) ? PFS_ROOT
                                     : nb_pfs_nodes[cur_dir].parent;

    c = pfs_first_child(cur_dir);
    while (c != PFS_NONE && n < i)
    {
        c = pfs_next_child(cur_dir, c);
        n++;
    }
    return c;
}

/* one word of a file, compared without regard to case */
static int tok_is(const unsigned char *s, const unsigned char *e,
                  const char *w)
{
    while (s < e && *w)
    {
        unsigned char a = *s++;
        unsigned char b = (unsigned char)*w++;

        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return s == e && !*w;
}

/*
 * Is this file a program rather than a document?
 *
 * The first line of a file that is neither blank nor a comment may name
 * what the file is for -- "program = preferences" -- and a file that
 * does name one starts that program when it is chosen in the browser
 * instead of opening in NeoText.  That is how a drawer holds programs
 * the way the start menu holds them: Config/ carries the Preferences
 * entry beside the four files it reads at boot, and any other directory
 * can carry whatever the desktop can run.
 *
 * A file that says nothing, or that names a program the desktop does
 * not have, is a document and opens in the reader as it always has, so
 * a typo costs the shortcut and nothing else.  Answers the start
 * menu's program slot, or -1 for a document.
 */
static int program_slot_of(unsigned t)
{
    static const char *const names[N_PROGRAMS] = {
        "files", "clock", "monitor", "about", "preferences", "neotext"
    };
    const unsigned char *p = nb_pfs_nodes[t].data;
    const unsigned char *end = p + nb_pfs_nodes[t].size;
    int ret = -1;

    while (p < end)
    {
        const unsigned char *eol = p;
        const unsigned char *k, *eq, *ke, *v, *ve;
        int i;

        while (eol < end && *eol != '\n')
            eol++;

        k = p;
        while (k < eol && (*k == ' ' || *k == '\t'))
            k++;
        if (k >= eol || *k == '#')          /* blank or a comment line   */
        {
            p = (eol < end) ? eol + 1 : end;
            continue;
        }

        /* the first line with words in it is the one that decides */
        eq = k;
        while (eq < eol && *eq != '=')
            eq++;
        if (eq > k && eq < eol)
        {
            ke = eq;
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
                ke--;
            v = eq + 1;
            while (v < eol && (*v == ' ' || *v == '\t'))
                v++;
            ve = eol;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' ||
                              ve[-1] == '\r'))
                ve--;
            if (tok_is(k, ke, "program"))
                for (i = 0; i < N_PROGRAMS; i++)
                    if (tok_is(v, ve, names[i]))
                        ret = i;
        }
        break;
    }
    return ret;
}

/*
 * How many rows the list is standing: row 0 is always the level above,
 * and the rest are the children as far as they run out or the window
 * does.  A pointer finds this out by landing on nothing, which is a
 * luxury the keyboard does not have -- it has to know where the list
 * ends before it moves, or it walks the light off the bottom.
 */
static int row_count(void)
{
    unsigned child = pfs_first_child(cur_dir);
    int n = 1;

    while (child != PFS_NONE && n < FILES_ROWS)
    {
        n++;
        child = pfs_next_child(cur_dir, child);
    }
    return n;
}

/*
 * The chosen list row's light: a wash across its full width, under the
 * name and the size, so the double click's first press is visible on
 * the line it landed on rather than only in the serial log.
 */
static void row_light(int i)
{
    int ry;

    if (sel_kind != SEL_ROW || sel_idx != i)
        return;

    /* Workbench's listview reverses the chosen row: a solid blue field
     * the whole width of the list, with the type set white on it */
    ry = FILES_Y + FILES_ROW0 + i * FILES_ROWH;
    gfx_fill(FILES_X + 8, ry, FILES_W - 16, FILES_ROWH - 1, C_WB_BLUE);
}

static void window_files(void)
{
    const int x = FILES_X, y = FILES_Y, w = FILES_W, h = FILES_H;
    char title[28];
    char buf[16];
    unsigned child, count = 0;
    int i, ry, on;
    char *d;

    path_title(title, sizeof(title));
    glass_window(x, y, w, h, 20, title, 3);

    text_d(x + 10, y + FILES_HEAD, "Name", C_MUTE);
    col_r(x + w - 10, y + FILES_HEAD, "Size");
    gfx_fill(x + 10, y + 40, w - 20, 1, C_WB_SHADE);

    /* row 0: up one level */
    on = (sel_kind == SEL_ROW && sel_idx == 0);
    row_light(0);
    row_glyph(0, x + 10, y + FILES_ROW0 + 1);
    text_d(x + 30, y + FILES_ROW0 + 3, "..", on ? C_INK : C_TEXT);
    col_r(x + w - 10, y + FILES_ROW0 + 3, "up");

    child = pfs_first_child(cur_dir);
    for (i = 1; i < FILES_ROWS; i++)
    {
        const struct pfs_node *nd;
        int prog;

        ry = y + FILES_ROW0 + i * FILES_ROWH;
        if (child == PFS_NONE)
            break;

        nd = &nb_pfs_nodes[child];
        on = (sel_kind == SEL_ROW && sel_idx == i);
        row_light(i);
        prog = nd->dir ? -1 : program_slot_of(child);
        if (prog >= 0)
        {
            /* a program wears the tile its entry wears in the start
             * menu, so the same program is the same colour in both */
            menu_glyph(prog, x + 10, ry + 4);
        }
        else
            row_glyph(nd->dir ? 1 : 2, x + 10, ry + 1);
        text_d(x + 30, ry + 3, nd->name, on ? C_INK : C_TEXT);
        if (nd->dir)
            col_r(x + w - 10, ry + 3, "<DIR>");
        else if (prog >= 0)
            col_r(x + w - 10, ry + 3, "run");
        else
        {
            fmt_size(buf, nd->size);
            col_r(x + w - 10, ry + 3, buf);
        }
        child = pfs_next_child(cur_dir, child);
    }

    for (child = pfs_first_child(cur_dir); child != PFS_NONE;
         child = pfs_next_child(cur_dir, child))
        count++;

    gfx_fill(x + 10, y + 218, w - 20, 1, C_WB_SHADE);
    d = put_num(buf, count);
    d = put_str(d, count == 1u ? " object" : " objects");
    *d = '\0';
    text_d(x + 10, y + 226, buf, C_MUTE);
}

/* The chosen item's light goes out, and it claims its own rows as it
 * does -- so the repaint that follows takes the highlight with it. */
static int sel_clear(void)
{
    if (sel_kind == SEL_NONE)
        return 0;
    band_select(sel_kind, sel_idx);
    sel_kind = SEL_NONE;
    return 1;
}

/* ------------------------------------------------------------------ *
 * Preferences: the backdrop, in one pane
 * ------------------------------------------------------------------ */

/*
 * A press that lands inside a window with no gadget under it takes the
 * keyboard to that window: the lit button on the bar follows the
 * pointer, which is what makes the bar's claim that it shows the window
 * you are in true rather than hopeful.  Returns 1, because the press
 * was this window's however little of it there was to answer.
 */
static int focus_here(int p, int *changed)
{
    if (focus_p != p)
    {
        focus_p = p;
        band_add(486, 512);
        *changed = 1;
    }
    return 1;
}

/* ------------------------------------------------------------------ *
 * Moving a window
 * ------------------------------------------------------------------ */

static int drag_win;            /* which window the pointer is carrying,
                                 * 0 when nothing is held              */
static int drag_ox, drag_oy;    /* where into it the pointer went down */

/*
 * The box as the drag pass asks for it: where the window stands and
 * how big it was laid out.  The four program windows answer with their
 * corner; the dial answers with the square its centre stands in, which
 * is the one base in this file that is a centre rather than a corner
 * and is squared up here rather than at every call.  Everything the
 * drag does is asked in these terms, so the offset, the clamp and the
 * two bands are one piece of code for all six.
 */
static void win_box(int which, int *x, int *y, int *w, int *h)
{
    switch (which)
    {
    case 1:  *x = FILES_X; *y = FILES_Y; *w = FILES_W; *h = FILES_H; break;
    case 2:  *x = CLOCK_X - CLOCK_R; *y = CLOCK_Y - CLOCK_R;
             *w = CLOCK_R * 2; *h = CLOCK_R * 2; break;
    case 3:  *x = MON_X; *y = MON_Y; *w = MON_W; *h = MON_H; break;
    case 4:  *x = AB_X; *y = AB_Y; *w = AB_W; *h = AB_H; break;
    case 5:  *x = PR_X; *y = PR_Y; *w = PR_W; *h = PR_H; break;
    default: *x = NT_X; *y = NT_Y; *w = NT_W; *h = NT_H; break;
    }
}

/*
 * Put it down somewhere new: the offset against the base that is a
 * constant in the ROM image.  This is the only write any of the six
 * positions is, and it lands in .bss, which is the part of the image
 * that is RAM.
 */
static void win_place(int which, int x, int y)
{
    switch (which)
    {
    case 1:  files_dx = x - 376; files_dy = y - 176; break;
    case 2:  clock_dx = (x + CLOCK_R) - 590;
             clock_dy = (y + CLOCK_R) - 76; break;
    case 3:  mon_dx = x - 452; mon_dy = y - 116; break;
    case 4:  about_dx = x - 124; about_dy = y - 40; break;
    case 5:  prefs_dx = x - 110; prefs_dy = y - 150; break;
    default: text_dx = x - 60; text_dy = y - 150; break;
    }
}

/* The two ranges a move claims: where it was standing, and where it is
 * now.  A band is a run of whole rows, so the left and right edges of a
 * window are covered by the same two claims as its top and bottom, and
 * a window that has crossed another's rows repaints them both -- which
 * is the point of claiming before and after rather than the difference
 * between the two. */
static void win_band(int which, int bar)
{
    switch (which)
    {
    case 1:  band_files(bar); break;
    case 2:  band_clock(); break;
    case 3:  band_monitor(); break;
    case 4:  band_about(); break;
    case 5:  band_prefs(bar); break;
    default: band_neotext(bar); break;
    }
}

/*
 * The press that picks a window up.  It gives the window the keyboard
 * -- the one being moved is the one being used, which is how the rest
 * of the desktop already treats a press inside a window -- and then
 * takes the pointer itself until the button comes back up.  It names
 * nothing, so nothing lights: a drag is not a selection, and the
 * second press that would run an item has to come from a press that
 * stood still.
 */
static int drag_here(int which, int x, int y, int *changed)
{
    int wx, wy, ww, wh;

    win_box(which, &wx, &wy, &ww, &wh);
    focus_here(which, changed);
    drag_win = which;
    drag_ox = x - wx;
    drag_oy = y - wy;
    return 1;
}

/*
 * Every field while the button is down: carry the window the press came
 * down on and claim the rows it has left and the rows it has taken.  It
 * answers the way a press answers -- non-zero when the answer changed
 * what should be on screen -- so the main loop takes the same three
 * calls behind it, and the log gets the same one line, which is how a
 * move can be read back as the position it passed through.
 *
 * What is clamped is the caption rather than the window: a window may
 * stand half off an edge, as they do on any desktop, but enough of the
 * strip has to stay on the screen to pick it up by again.
 */
int nb_desktop_drag(void)
{
    int wx, wy, ww, wh, nx, ny;

    if (drag_win == 0)
        return 0;
    if (!(nb_pointer_held() & NB_BTN_L))
    {
        drag_win = 0;               /* the button came up: it lands here  */
        return 0;
    }

    win_box(drag_win, &wx, &wy, &ww, &wh);
    nx = nb_pointer_x() - drag_ox;
    ny = nb_pointer_y() - drag_oy;

    if (nx < 48 - ww)
        nx = 48 - ww;
    if (nx > 640 - 48)
        nx = 640 - 48;
    if (ny < 0)
        ny = 0;
    if (ny > 512 - 24)
        ny = 512 - 24;
    if (nx == wx && ny == wy)
        return 0;

    win_band(drag_win, 0);          /* the rows it was standing in        */
    win_place(drag_win, nx, ny);
    win_band(drag_win, 0);          /* and the rows it now covers         */
    return 1;
}

/* a Workbench check box: a sunken square, a tick when it is on, and the
 * label beside it -- box and label together are one control, because
 * that is the size the eye reads them at */
static void prefs_box(int x, int y, int w, int h, const char *label, int on)
{
    gfx_fill(x, y, w, h, C_WB_GREY);
    gfx_fill(x + 1, y + 1, w - 2, 1, C_WB_SHADE);
    gfx_fill(x + 1, y + 1, 1, h - 2, C_WB_SHADE);
    gfx_fill(x, y + h - 1, w, 1, C_INK);
    gfx_fill(x + w - 1, y, 1, h, C_INK);
    gfx_fill(x + 1, y + 1, w - 2, h - 2, C_INK);

    if (on)
    {
        gfx_fill(x + 2, y + 5, 2, 2, C_WB_BLUE);
        gfx_fill(x + 3, y + 7, 2, 2, C_WB_BLUE);
        gfx_fill(x + 5, y + 4, 2, 3, C_WB_BLUE);
        gfx_fill(x + 6, y + 3, 2, 2, C_WB_BLUE);
    }

    text_d(x + w + 6, y + (h - 8) / 2, label, C_TEXT);
}

/* a one pixel ring round whatever the keys are standing on: four fills,
 * because there is no outline worth a routine of its own */
static void pr_ring(int x, int y, int w, int h)
{
    gfx_fill(x, y, w, 1, C_WB_LINE);
    gfx_fill(x, y + h - 1, w, 1, C_WB_LINE);
    gfx_fill(x, y, 1, h, C_WB_LINE);
    gfx_fill(x + w - 1, y, 1, h, C_WB_LINE);
}

/*
 * Preferences: one pane, five swatches, two check boxes.
 *
 * The swatches are the same five Config/screen.cfg names and the same
 * five the wallpaper paints; choosing one changes the backdrop at once,
 * which is the whole reason the pane exists.  A swatch is chosen rather
 * than opened, so it is a control and takes one press as the cross
 * does -- and it claims the whole raster when it repaints, because what
 * changed was the backdrop under everything else as well.
 *
 * The two layers below are the flags screen.cfg already carries.  A
 * preference nobody can see is one nobody will ever find, so they stand
 * here as much to be seen as to be changed.
 */
static void window_prefs(void)
{
    const int x = PR_X, y = PR_Y, w = PR_W, h = PR_H;
    uint16_t top, bot;
    int i;

    glass_window(x, y, w, h, 20, "Preferences", 3);

    text_d(x + 10, y + 30, "Backdrop", C_MUTE);

    for (i = 0; i < NB_BD_COUNT; i++)
    {
        const char *nm = nb_bd_name(i);
        int sx = x + PR_S0 + i * (PR_SW + 4);
        int sel = (nb_prefs.backdrop == i);

        nb_bd_colours(i, &top, &bot);
        gfx_fill(sx - 2, y + PR_SY - 2, PR_SW + 4, PR_SH + 4,
                 sel ? C_WB_BLUE : C_WB_LINE);
        gfx_vgrad(sx, y + PR_SY, PR_SW, PR_SH, top, bot);
        text_d(sx + (PR_SW - strw(nm)) / 2, y + PR_SY + PR_SH + 4,
               nm, sel ? C_WB_BLUE : C_MUTE);
    }

    gfx_fill(x + 10, y + 86, w - 20, 1, C_WB_GREY);

    text_d(x + 10, y + 94, "Layers", C_MUTE);
    prefs_box(x + 10, y + PR_LAY, 11, 11, "Grid", nb_prefs.grid);
    prefs_box(x + 90, y + PR_LAY, 11, 11, "Glow", nb_prefs.glow);

    /*
     * Where the keyboard is, drawn only while the pane has it: a ring
     * round the row the keys work on, which is a different thing from
     * the blue frame that says which backdrop is the one in force -- one
     * is where you are, the other is what is on.
     */
    if (focus_p == 5)
    {
        if (pr_lay == 0)
            pr_ring(x + 6, y + PR_SY - 4, w - 12, PR_SH + 8);
        else
            pr_ring(pr_lay == 1 ? x + 7 : x + 87, y + PR_LAY - 3, 56, 17);
    }

    text_d(x + 10, y + 136, "Applies at once.", C_MUTE);
    text_d(x + 10, y + 148, "Config/screen.cfg sets", C_MUTE);
    text_d(x + 10, y + 160, "what the boot comes up as.", C_MUTE);
}

/*
 * The pane's keys, while the pane is the program with the keyboard.
 * Up and down move between the three rows it has -- the swatches, the
 * grid box, the glow box -- and left and right work whichever is
 * standing: the swatches are a control, so they paint the moment they
 * are chosen, and the boxes are check boxes, so left puts one out and
 * right lights it.  A pane only reachable by a pointer would be a pane
 * half the machine cannot use, and every other control here already
 * answers to the keys.
 *
 * Returns non-zero when the key did something, which is what the caller
 * owes a repaint for.
 */
static int prefs_key(int c)
{
    if (c == NB_KEY_UP || c == NB_KEY_DOWN)
    {
        int row = pr_lay + ((c == NB_KEY_DOWN) ? 1 : -1);

        if (row < 0)
            row = 0;
        if (row > 2)
            row = 2;
        if (row == pr_lay)
            return 0;
        pr_lay = row;
        band_prefs(0);              /* the ring moved, nothing else did  */
        return 1;
    }

    if (c != NB_KEY_LEFT && c != NB_KEY_RIGHT &&
        c != NB_KEY_RET && c != ' ')
        return 0;

    if (pr_lay == 0)
    {
        int want;

        if (c == NB_KEY_RET || c == ' ')
            return 0;               /* it is already painted             */

        want = nb_prefs.backdrop + ((c == NB_KEY_RIGHT) ? 1 : -1);
        if (want < 0)
            want = 0;
        if (want >= NB_BD_COUNT)
            want = NB_BD_COUNT - 1;
        if (want == nb_prefs.backdrop)
            return 0;
        nb_prefs.backdrop = want;
        band_add(0, 512);           /* the backdrop is under it all      */
        return 1;
    }
    else
    {
        int *flag = (pr_lay == 1) ? &nb_prefs.grid : &nb_prefs.glow;
        int want;

        if (c == NB_KEY_LEFT)
            want = 0;
        else if (c == NB_KEY_RIGHT)
            want = 1;
        else
            want = !*flag;          /* return or space: turn it over     */
        if (*flag == want)
            return 0;
        *flag = want;
        band_add(0, 512);           /* both layers are over the field    */
        return 1;
    }
}

/* the pane's press: its cross, one of the five, or a layer.  0 when the
 * press was not in the pane at all, 1 when it was and is spent. */
static int hit_prefs(int x, int y, int *changed)
{
    int i;

    if (!prefs_open ||
        x < PR_X || x >= PR_X + PR_W || y < PR_Y || y >= PR_Y + PR_H)
        return 0;

    if (on_close(x, y, PR_X, PR_Y, PR_W))
    {
        prefs_open = 0;
        if (focus_p == 5)
            focus_p = 0;
        sel_clear();
        band_prefs(1);
        *changed = 1;
        return 1;
    }

    if (on_caption(x, y, PR_X, PR_Y, PR_W, 3))
        return drag_here(5, x, y, changed);

    for (i = 0; i < NB_BD_COUNT; i++)
    {
        int sx = PR_X + PR_S0 + i * (PR_SW + 4);
        int sy = PR_Y + PR_SY;

        if (x < sx || x >= sx + PR_SW || y < sy || y >= sy + PR_SH)
            continue;

        if (nb_prefs.backdrop != i)
        {
            nb_prefs.backdrop = i;
            band_add(0, 512);           /* the backdrop is under it all  */
            *changed = 1;
        }
        if (focus_p != 5)
        {
            focus_p = 5;
            band_add(486, 512);
            *changed = 1;
        }
        return 1;
    }

    if (y >= PR_Y + PR_LAY && y < PR_Y + PR_LAY + 11)
    {
        int *flag = 0;

        if (x >= PR_X + 10 && x < PR_X + 62)
            flag = &nb_prefs.grid;
        else if (x >= PR_X + 90 && x < PR_X + 142)
            flag = &nb_prefs.glow;

        if (!flag)
            return focus_here(5, changed);

        *flag = !*flag;
        band_add(0, 512);               /* the layers are over the field */
        focus_p = 5;
        *changed = 1;
        return 1;
    }

    return focus_here(5, changed);
}

/* ------------------------------------------------------------------ *
 * NeoText: every file in the store, in one window
 * ------------------------------------------------------------------ */

#define NT_TEXT 0               /* shown as written, and typed into      */
#define NT_PDF  1               /* a PDF's text, lifted out of its pages */
#define NT_HEX  2               /* anything else, as the bytes it is     */

static unsigned char nt_doc[24576];   /* what is shown, and typed into   */
static unsigned char nt_scr[32768];   /* inflate room, while a PDF reads */
static unsigned nt_len;               /* bytes in nt_doc                 */
static unsigned nt_cur;               /* the caret: an offset in it      */
static unsigned nt_top;               /* the first row of the view       */
static int      nt_mode;              /* NT_TEXT, NT_PDF or NT_HEX       */
static int      nt_have;              /* a file has been brought up      */
static int      nt_more;              /* the file did not all fit        */
static int      nt_dirty;             /* typed into since it was read    */
static char     nt_name[24];          /* the file, for the title         */

/*
 * Where the next row begins, from the start of this one: a newline, a
 * hard wrap at the pane's width, or the end.  The newline is looked for
 * before the wrap is counted, so a line exactly as wide as the pane
 * does not leave an empty row behind it; and no carriage return can
 * reach here, because neotext_load drops them as it reads, which is
 * why a file written on another machine shows the same lines here.
 */
static unsigned nt_next(unsigned off)
{
    unsigned n = 0;

    while (off < nt_len)
    {
        unsigned char ch = nt_doc[off];

        if (ch == '\n')
            return off + 1;
        if (n == NT_COLS)
            return off;
        off++;
        n++;
    }
    return off;
}

/* the byte offset row r starts at: eight bytes a row in the hex view,
 * and a walk of the rows before it anywhere else */
static unsigned nt_rowstart(unsigned r)
{
    unsigned off = 0, k;

    if (nt_mode == NT_HEX)
        return r * 8;

    for (k = 0; k < r; k++)
    {
        unsigned nx = nt_next(off);

        if (nx <= off)
            break;
        off = nx;
    }
    return off;
}

/*
 * How many rows the view has to scroll through.  The rows are the
 * segments the row starts cut, so a newline ends a row rather than
 * starting one: "abc" and "abc\n" are both one line, and the empty
 * thing after the second is the same nothing that follows the first.
 */
static unsigned nt_rows_total(void)
{
    unsigned off = 0, r = 0;

    if (nt_mode == NT_HEX)
    {
        if (nt_len == 0)
            return 1;
        return (nt_len + 7) / 8;
    }

    while (off < nt_len)
    {
        unsigned nx = nt_next(off);

        r++;
        if (nx <= off)
            break;
        off = nx;
    }
    return r ? r : 1;
}

/* the row the caret is on: the row starts it stands past, with a caret
 * resting at the end of the file put back on the file's own last row */
static unsigned nt_currow(void)
{
    unsigned off = 0, r = 0, total;

    while (off < nt_cur)
    {
        unsigned nx = nt_next(off);

        if (nx <= off || nx > nt_cur)
            break;
        off = nx;
        r++;
    }

    total = nt_rows_total();
    if (r >= total)
        r = total - 1;
    return r;
}

/* the row the status line reports: the caret in the mode that has one,
 * and the top of the view in the two that do not */
static unsigned nt_where(void)
{
    if (nt_mode == NT_TEXT)
        return nt_currow();
    return nt_top;
}

/*
 * What row r shows, into buf: the row's own characters for a document,
 * or eight bytes as hex for anything that is not one.  Fifty-five
 * characters of text and forty-three of hex are the most it can be, so
 * callers pass sixty-four.
 */
static unsigned nt_rowline(unsigned r, char *buf)
{
    static const char hx[] = "0123456789abcdef";
    unsigned off, end, n = 0, i;

    if (nt_mode == NT_HEX)
    {
        unsigned base = r * 8;

        for (i = 0; i < 8; i++)
            buf[n++] = hx[(base >> ((7 - i) * 4)) & 0xF];
        buf[n++] = ' ';
        buf[n++] = ' ';

        for (i = 0; i < 8; i++)
        {
            if (base + i < nt_len)
            {
                unsigned char ch = nt_doc[base + i];

                buf[n++] = hx[ch >> 4];
                buf[n++] = hx[ch & 0xF];
            }
            else
            {
                buf[n++] = ' ';
                buf[n++] = ' ';
            }
            buf[n++] = ' ';
        }
        buf[n++] = ' ';

        for (i = 0; i < 8; i++)
        {
            unsigned char ch = 0;

            if (base + i < nt_len)
                ch = nt_doc[base + i];

            if (base + i >= nt_len)
                buf[n++] = ' ';
            else if (ch >= 32 && ch < 127)
                buf[n++] = (char)ch;
            else
                buf[n++] = '.';
        }

        buf[n] = '\0';
        return n;
    }

    off = nt_rowstart(r);
    end = nt_next(off);
    while (off < end)
    {
        unsigned char ch = nt_doc[off];

        if (ch == '\n')
            break;
        if (ch == '\t')
            ch = ' ';                   /* one column, as it was one byte */
        else if (ch < 32 || ch > 126)
            ch = '.';
        buf[n++] = (char)ch;
        off++;
    }
    buf[n] = '\0';
    return n;
}

/* keep the caret's row inside the view -- the rule that makes a long
 * file follow the cursor instead of waiting for a scrollbar */
static void nt_follow(void)
{
    unsigned row = nt_currow();

    if (row < nt_top)
        nt_top = row;
    else if (row >= nt_top + NT_ROWS)
        nt_top = row - NT_ROWS + 1;
}

/*
 * Bring a file into the reader.  Three answers, and only three: text,
 * shown as written and the only one that can be typed into; a PDF,
 * whose text is lifted out of the page and shown the same way; and
 * everything else, shown as the bytes it is.  The first is what the
 * store holds most of, the second is what Docs/guide.pdf is there to be,
 * and the third is the honest answer for the rest -- NeoBench does not
 * pretend to know what an executable or a sound file says about itself.
 */
static void neotext_load(unsigned node)
{
    const unsigned char *src;
    unsigned size, i, n = 0;
    unsigned probe, good;
    int textish;

    nt_len = nt_cur = nt_top = 0;
    nt_mode = NT_TEXT;
    nt_have = nt_more = nt_dirty = 0;
    nt_name[0] = '\0';

    if (node >= nb_pfs_count || nb_pfs_nodes[node].dir ||
        nb_pfs_nodes[node].size == 0)
        return;

    nt_have = 1;
    src = nb_pfs_nodes[node].data;
    size = nb_pfs_nodes[node].size;

    for (i = 0; i + 1 < sizeof(nt_name) && nb_pfs_nodes[node].name[i]; i++)
        nt_name[i] = nb_pfs_nodes[node].name[i];
    nt_name[i] = '\0';

    /* a PDF says so in its first five bytes, whatever it is called */
    if (size >= 5 && src[0] == '%' && src[1] == 'P' && src[2] == 'D' &&
        src[3] == 'F' && src[4] == '-')
    {
        int ntxt = nb_pdf_text(src, size, (char *)nt_doc, sizeof(nt_doc),
                               nt_scr, sizeof(nt_scr));

        if (ntxt > 0)
        {
            nt_len = (unsigned)ntxt;
            nt_mode = NT_PDF;
            return;
        }
        /* a document with no text in it falls through to the bytes,
         * which is the only true thing to say about it */
    }

    /* text or bytes: the first two kilobytes decide, nine parts in ten.
     * A file that is nearly all printable is a text file, and one that
     * is not, is lying in wait. */
    probe = (size < 2048) ? size : 2048;
    good = 0;
    for (i = 0; i < probe; i++)
    {
        unsigned char ch = src[i];

        if (ch == '\t' || ch == '\n' || ch == '\r' ||
            (ch >= 32 && ch < 127))
            good++;
    }
    textish = (good * 10 >= probe * 9);

    for (i = 0; i < size && n + 1 < sizeof(nt_doc); i++)
    {
        unsigned char ch = src[i];

        if (textish && ch == '\r')
            continue;                   /* a line is a line anywhere     */
        nt_doc[n++] = ch;
    }
    nt_len = n;
    nt_doc[n] = '\0';
    nt_mode = textish ? NT_TEXT : NT_HEX;
    nt_more = (i < size);
    nt_follow();
}

/* one end of the scroll column: a small raised button with a triangle
 * the right way up.  It is a control, so it takes one press. */
static void nt_button(int x, int y, int up)
{
    gfx_fill(x, y, 8, NT_SBT, C_WB_GREY);
    gfx_fill(x, y, 8, 1, C_INK);
    gfx_fill(x, y, 1, NT_SBT, C_INK);
    gfx_fill(x, y + NT_SBT - 1, 8, 1, C_WB_SHADE);
    gfx_fill(x + 7, y, 1, NT_SBT, C_WB_SHADE);

    if (up)
        gfx_tri(x + 1, y + 9, x + 7, y + 9, x + 4, y + 3, C_WB_LINE);
    else
        gfx_tri(x + 1, y + 2, x + 7, y + 2, x + 4, y + 8, C_WB_LINE);
}

/* move the view without moving the caret: a reader scrolls by the rows
 * it shows, and only the caret moves the view when the caret moved */
static int neotext_scroll(int rows)
{
    unsigned total = nt_rows_total();
    int top = (int)nt_top + rows;

    if (top < 0)
        top = 0;
    if (total > (unsigned)NT_ROWS &&
        (unsigned)top > total - (unsigned)NT_ROWS)
        top = (int)(total - (unsigned)NT_ROWS);
    if ((unsigned)top == nt_top)
        return 0;
    nt_top = (unsigned)top;
    return 1;
}

/*
 * NeoText: text, the text of a PDF, or the bytes of anything else, in
 * one window that follows its caret.  The two read-only modes have no
 * caret because there is nothing to type into them, and they say so on
 * the status line rather than letting an edit go quietly nowhere.
 */
static void window_neotext(void)
{
    const int x = NT_X, y = NT_Y, w = NT_W, h = NT_H;
    char title[40], line[64], left[40], right[40];
    unsigned total, r;
    char *d;

    d = put_str(title, "NeoText");
    if (nt_have)
    {
        d = put_str(d, ": ");
        d = put_str(d, nt_name);
    }
    *d = '\0';
    glass_window(x, y, w, h, 20, title, 3);

    if (!nt_have)
    {
        text_d(x + 14, y + 34, "No file is open.", C_MUTE);
        text_d(x + 14, y + 46, "Choose one in Files.", C_MUTE);
        gfx_fill(x + 10, y + h - 26, w - 20, 1, C_WB_SHADE);
        text_d(x + 10, y + h - 16, "(no file)", C_MUTE);
        return;
    }

    /*
     * The scroll column: a groove with an arrow at each end.  Where you
     * are in the file is the status line's to say rather than a thumb's
     * to show -- "row 12 of 84" is exact, and a thumb would want the
     * ratio of two numbers that are not known until the file is opened.
     */
    gfx_fill(NT_SB, NT_SBY + NT_SBT, 8, NT_SBTR, C_WB_GREY);
    gfx_fill(NT_SB, NT_SBY + NT_SBT, 8, 1, C_WB_SHADE);
    gfx_fill(NT_SB, NT_SBY + NT_SBT, 1, NT_SBTR, C_WB_SHADE);
    gfx_fill(NT_SB, NT_SBY + NT_SBT + NT_SBTR - 1, 8, 1, C_INK);
    gfx_fill(NT_SB + 7, NT_SBY + NT_SBT, 1, NT_SBTR, C_INK);
    nt_button(NT_SB, NT_SBY, 1);
    nt_button(NT_SB, NT_SBY + NT_SBH - NT_SBT, 0);

    total = nt_rows_total();
    for (r = nt_top; r < nt_top + NT_ROWS; r++)
    {
        nt_rowline(r, line);
        text_d(NT_TX, (int)(NT_TY + (r - nt_top) * 9), line, C_TEXT);
    }

    /* the caret, when there is one and the view has it */
    if (nt_mode == NT_TEXT)
    {
        unsigned row = nt_currow();

        if (row >= nt_top && row < nt_top + NT_ROWS)
        {
            int cx = NT_TX + (int)(nt_cur - nt_rowstart(row)) * 8;
            int cy = (int)(NT_TY + (row - nt_top) * 9);

            if (cx > NT_TX + NT_COLS * 8)
                cx = NT_TX + NT_COLS * 8;
            gfx_fill(cx, cy, 1, 9, C_WB_BLUE);
        }
    }

    gfx_fill(x + 10, y + h - 26, w - 20, 1, C_WB_SHADE);

    /* what is being shown, and what it cannot do about it */
    d = put_str(left, nt_mode == NT_HEX ? "hex" :
                      nt_mode == NT_PDF  ? "pdf text" : "text");
    d = put_str(d, "  row ");
    d = put_num(d, nt_where() + 1);
    d = put_str(d, " of ");
    d = put_num(d, total);
    *d = '\0';
    text_d(x + 10, y + h - 16, left, C_MUTE);

    if (nt_dirty)
        put_str(right, "(edited, not saved)");
    else if (nt_more)
        put_str(right, "(first 24 KB)");
    else if (nt_mode == NT_TEXT)
        put_str(right, "(store is read-only)");
    else
        put_str(right, "(read-only)");
    text_right(x + w - 10, y + h - 16, right, C_MUTE);
}

/* the caret a row up or down: the column it is in, taken to the row
 * above or below -- which a hard wrap cut as much as a newline did --
 * and stopped at that row's end if it is the shorter one */
static int nt_caret_v(int dir)
{
    unsigned row = nt_currow();
    unsigned start = nt_rowstart(row);
    unsigned col = nt_cur - start;
    unsigned target, end, k = 0;

    if (dir < 0)
    {
        if (row == 0)
            return 0;
        target = nt_rowstart(row - 1);
    }
    else
    {
        target = nt_next(start);
        if (target >= nt_len || target <= start)
            return 0;
    }

    end = nt_next(target);
    while (k < col && target + k < end && nt_doc[target + k] != '\n')
        k++;
    target += k;

    if (target == nt_cur)
        return 0;
    nt_cur = target;
    nt_follow();
    return 1;
}

static int nt_caret_h(int dir)
{
    if (dir < 0)
    {
        if (nt_cur == 0)
            return 0;
        nt_cur--;
    }
    else
    {
        if (nt_cur >= nt_len)
            return 0;
        nt_cur++;
    }
    nt_follow();
    return 1;
}

/* put a character where the caret stands, and take one away behind it.
 * The buffer is the file for as long as it holds it, and nt_more is
 * what says when it stopped holding all of it. */
static int nt_insert(int ch)
{
    unsigned i;

    if (nt_len + 1 >= sizeof(nt_doc))
        return 0;

    for (i = nt_len; i > nt_cur; i--)
        nt_doc[i] = nt_doc[i - 1];
    nt_doc[nt_cur] = (unsigned char)ch;
    nt_len++;
    nt_cur++;
    nt_doc[nt_len] = '\0';
    nt_dirty = 1;
    nt_follow();
    return 1;
}

static int nt_back(void)
{
    unsigned i;

    if (nt_cur == 0)
        return 0;
    for (i = nt_cur - 1; i + 1 < nt_len; i++)
        nt_doc[i] = nt_doc[i + 1];
    nt_len--;
    nt_cur--;
    nt_doc[nt_len] = '\0';
    nt_dirty = 1;
    nt_follow();
    return 1;
}

/*
 * The reader's keys, and it only has them while it is the program with
 * the keyboard.  Returns non-zero when the key did something, which is
 * what the caller owes a repaint for.
 *
 * The cursor keys step the caret in the mode that has one and the view
 * in the two that do not; Tab is the page the keymap has no key for;
 * Return starts a line; and a character puts itself where the caret
 * stands.  Everything else is somebody else's key.
 */
static int neotext_key(int c)
{
    int ro = (nt_mode != NT_TEXT);

    if (!nt_have)
        return 0;

    if (c == NB_KEY_UP || c == NB_KEY_DOWN)
    {
        if (ro)
            return neotext_scroll(c == NB_KEY_UP ? -1 : 1);
        return nt_caret_v(c == NB_KEY_UP ? -1 : 1);
    }
    if (c == NB_KEY_LEFT || c == NB_KEY_RIGHT)
        return ro ? 0 : nt_caret_h(c == NB_KEY_LEFT ? -1 : 1);
    if (c == NB_KEY_TAB)
        return neotext_scroll(NT_ROWS);
    if (ro)
        return 0;

    if (c == NB_KEY_RET)
        return nt_insert('\n');
    if (c == NB_KEY_BACK)
        return nt_back();
    if (c >= 32 && c < 127)
        return nt_insert(c);
    return 0;
}

/* the reader's press: its cross, the scroll column, the text itself.
 * 0 when the press was not in the window, 1 when it was and is spent. */
static int hit_neotext(int x, int y, int *changed)
{
    if (!neotext_open ||
        x < NT_X || x >= NT_X + NT_W || y < NT_Y || y >= NT_Y + NT_H)
        return 0;

    if (on_close(x, y, NT_X, NT_Y, NT_W))
    {
        neotext_open = 0;
        if (focus_p == 6)
            focus_p = 0;
        sel_clear();
        band_neotext(1);
        *changed = 1;
        return 1;
    }

    if (on_caption(x, y, NT_X, NT_Y, NT_W, 3))
        return drag_here(6, x, y, changed);

    if (nt_have && x >= NT_SB && x < NT_SB + 8 &&
        y >= NT_SBY && y < NT_SBY + NT_SBH)
    {
        int moved;

        if (focus_p != 6)
        {
            focus_p = 6;
            band_add(486, 512);
            *changed = 1;
        }
        if (y < NT_SBY + NT_SBT)
            moved = neotext_scroll(-1);
        else if (y >= NT_SBY + NT_SBH - NT_SBT)
            moved = neotext_scroll(1);
        else
            moved = neotext_scroll(y < NT_SBMID ? -NT_ROWS : NT_ROWS);
        if (moved)
        {
            band_neotext(0);            /* the rows moved, nothing else  */
            *changed = 1;
        }
        return 1;
    }

    /* the text: a press in it puts the caret where it landed */
    if (nt_have && nt_mode == NT_TEXT &&
        x >= NT_TX && x < NT_TX + NT_COLS * 8 &&
        y >= NT_TY && y < NT_TY + NT_ROWS * 9)
    {
        unsigned row = nt_top + (unsigned)((y - NT_TY) / 9);
        unsigned col = (unsigned)(x - NT_TX + 4) / 8;
        unsigned start, end, k = 0;

        if (row >= nt_rows_total())
            row = nt_rows_total() - 1;
        start = nt_rowstart(row);
        end = nt_next(start);
        while (k < col && start + k < end && nt_doc[start + k] != '\n')
            k++;
        nt_cur = start + k;
        nt_follow();
        if (focus_p != 6)
        {
            focus_p = 6;
            band_add(486, 512);
        }
        band_neotext(0);                /* the caret stands somewhere new */
        *changed = 1;
        return 1;
    }

    return focus_here(6, changed);
}

/*
 * Show desktop: the narrow slab on the right-hand end of the panel.
 *
 * One press takes every program window off the wallpaper at once, the
 * next puts them back exactly where they were -- the flags are the
 * whole of what a window is, so saving six bits saves the desktop.
 * Like the orb and the crosses it is a control rather than an item, so
 * it answers to a single press.
 */
static int show_desktop(void)
{
    if (desk_hidden)
    {
        files_open   = (desk_saved & 1) != 0;
        clock_open   = (desk_saved & 2) != 0;
        monitor_open = (desk_saved & 4) != 0;
        about_open   = (desk_saved & 8) != 0;
        prefs_open   = (desk_saved & 16) != 0;
        neotext_open = (desk_saved & 32) != 0;
        desk_hidden = 0;
    }
    else
    {
        desk_saved = (files_open ? 1 : 0) | (clock_open ? 2 : 0) |
                     (monitor_open ? 4 : 0) | (about_open ? 8 : 0) |
                     (prefs_open ? 16 : 0) | (neotext_open ? 32 : 0);
        files_open = clock_open = monitor_open = about_open = 0;
        prefs_open = neotext_open = 0;
        if (focus_p)
            focus_p = 0;
        desk_hidden = 1;
    }
    band_programs();
    return 1;
}

/*
 * The near miss: the same item pressed twice without the second press
 * counting.  dt is the gap in fields (fifty to the second) and dx/dy
 * the drift between the two presses -- the two numbers the gate is
 * made of, and the only way to tune them from a serial log.
 */
static void dbl_report(unsigned dt, int dx, int dy)
{
    char buf[32], *d;

    d = put_str(buf, ">dbl dt=");
    d = put_num(d, dt);
    d = put_str(d, " dx=");
    d = put_snum(d, dx);
    d = put_str(d, " dy=");
    d = put_snum(d, dy);
    *d = '\0';

    for (d = buf; *d; d++)
        amiga_serial_putc(*d);
}

/*
 * The second press: run the item that was named by the first.
 *
 * Every arm here takes the rows it is about to change with it, and
 * gives the selection back whether or not it went anywhere -- which is
 * the only way a double click on the row you are already standing on
 * leaves the highlight honestly dark instead of painting a scene that
 * no longer matches what is set.
 */
/*
 * One of the six programs, standing or put away.
 *
 * The start menu asks for this as a toggle -- an entry whose program is
 * already showing puts it away, which is what the lit dot on the entry
 * means -- while a program filed in a drawer only ever asks for it to
 * be up: asking for Preferences twice means Preferences twice, exactly
 * as asking for a directory twice means that directory twice.  Either
 * way the answer goes to the panel, which shows the program that has
 * the keyboard, and to the bar, where the buttons light and go out.
 *
 * Returns non-zero when the program is standing afterwards.
 */
static int program_slot(int p, int toggle)
{
    int on;

    if (p < 0 || p >= N_PROGRAMS)
        return 0;

    switch (p)
    {
    case 0:  on = toggle ? !files_open   : 1; files_open   = on; break;
    case 1:  on = toggle ? !clock_open   : 1; clock_open   = on; break;
    case 2:  on = toggle ? !monitor_open : 1; monitor_open = on; break;
    case 3:  on = toggle ? !about_open   : 1; about_open   = on; break;
    case 4:  on = toggle ? !prefs_open   : 1;
             if (on)
                 pr_lay = 0;         /* the keys start on the swatches    */
             prefs_open = on;
             break;
    default: on = toggle ? !neotext_open : 1; neotext_open = on; break;
    }

    switch (p)                      /* the rows it and its button cover   */
    {
    case 0:  band_files(1);    break;
    case 1:  band_clock();     break;
    case 2:  band_monitor();   break;
    case 3:  band_about();     break;
    case 4:  band_prefs(1);    break;
    default: band_neotext(1);  break;
    }

    if (on)
    {
        if (focus_p != p + 1)
        {
            focus_p = p + 1;        /* the panel shows it as active       */
            band_add(486, 512);     /* and its button lights              */
        }
    }
    else if (focus_p == p + 1)
    {
        focus_p = 0;
        band_add(486, 512);
    }
    return on;
}

static int activate(int cls, int idx)
{
    int changed = sel_clear();

    if (cls == SEL_MENU)
    {
        changed = 1;
        if (!menu_sticky)               /* a sticky menu stays for more   */
        {
            menu_open = 0;
            band_menu();
        }

        if (idx < N_PLACES)
        {
            /*
             * A place brings the browser up on that directory.  Unlike a
             * program it does not toggle -- asking for Home twice means
             * Home twice, and putting the browser away is what the cross
             * is for -- but it does take the task manager's active
             * button, because the browser is what it started.
             */
            unsigned d = dir_of(places[idx].dir);

            if (!files_open || d != cur_dir || focus_p != 1)
            {
                cur_dir   = d;
                files_open = 1;
                focus_p    = 1;
                band_files(1);
            }
        }
        else
            /* a menu entry toggles: the lit dot says which are up */
            program_slot(menu_slot[idx - N_PLACES], 1);
    }
    else                                   /* a list row: it opens         */
    {
        unsigned t = row_target(idx);

        if (t != PFS_NONE && t != cur_dir)
        {
            if (nb_pfs_nodes[t].dir)
            {
                cur_dir = t;
                band_files(0);
                changed = 1;
            }
            else
            {
                int p = program_slot_of(t);     /* a program, or a file  */

                if (p >= 0)
                {
                    /*
                     * An entry in a drawer starts its program rather than
                     * opening in the reader, and asks for it to be up
                     * rather than for whatever it happens to be doing.
                     * The row is spent either way: choosing something is
                     * what the second press was for.
                     */
                    program_slot(p, 0);
                    changed = 1;
                }
                else
                {
                    /*
                     * A file, not a directory: it goes to the reader, which is
                     * what it has been waiting for.  The browser used to take
                     * the row's node as the directory to step into whether the
                     * node was one or not, which put the list on a file and left
                     * it with no children to walk -- the two answers were never
                     * meant to be the same answer.
                     */
                    neotext_load(t);
                    neotext_open = 1;
                    focus_p = 6;
                    band_neotext(1);        /* the window, and its button     */
                    changed = 1;
                }
            }
        }
    }

    return changed;
}

/*
 * One window's press, asked for in the order the draw pass paints them
 * backwards: its cross first, then whatever it carries inside itself.
 *
 * Returns 0 when the press was not in this window at all, 1 when it was
 * but named nothing -- it closed, scrolled, or only took the keyboard --
 * and 2 when it named a row, in which case cls and idx are set and the
 * double press gate decides what the naming was worth.
 */
static int win_press(int which, int x, int y, int *cls, int *idx,
                     int *changed)
{
    int i;

    switch (which)
    {
    case 1:                                     /* the browser            */
        if (!files_open ||
            x < FILES_X || x >= FILES_X + FILES_W ||
            y < FILES_Y || y >= FILES_Y + FILES_H)
            return 0;
        if (on_close(x, y, FILES_X, FILES_Y, FILES_W))
        {
            files_open = 0;
            if (focus_p == 1)
                focus_p = 0;
            sel_clear();
            band_files(1);
            *changed = 1;
            return 1;
        }
        if (on_caption(x, y, FILES_X, FILES_Y, FILES_W, 3))
            return drag_here(1, x, y, changed);
        for (i = 0; i < FILES_ROWS; i++)
        {
            int ry = FILES_Y + FILES_ROW0 + i * FILES_ROWH;

            if (y >= ry && y < ry + FILES_ROWH)
            {
                if (focus_p != 1)
                {
                    focus_p = 1;
                    band_add(486, 512);    /* its button lights           */
                    *changed = 1;
                }
                *cls = SEL_ROW;
                *idx = i;
                return 2;
            }
        }
        return focus_here(1, changed);

    case 2:                                     /* the clock                */
    {
        int dx = x - CLOCK_X, dy = y - CLOCK_Y;

        if (!clock_open || dx * dx + dy * dy > CLOCK_R * CLOCK_R)
            return 0;
        /* the dial has no furniture to pick it up by: the whole face
         * is the handle, and a press that stands still only lights it */
        return drag_here(2, x, y, changed);
    }

    case 3:                                     /* the monitor              */
        if (!monitor_open ||
            x < MON_X || x >= MON_X + MON_W ||
            y < MON_Y || y >= MON_Y + MON_H)
            return 0;
        return drag_here(3, x, y, changed);

    case 4:                                     /* the about panel          */
        if (!about_open ||
            x < AB_X || x >= AB_X + AB_W || y < AB_Y || y >= AB_Y + AB_H)
            return 0;
        if (on_close(x, y, AB_X, AB_Y, AB_W))
        {
            about_open = 0;
            if (focus_p == 4)
                focus_p = 0;
            sel_clear();
            band_about();
            *changed = 1;
            return 1;
        }
        if (on_caption(x, y, AB_X, AB_Y, AB_W, 3))
            return drag_here(4, x, y, changed);
        return focus_here(4, changed);

    case 5:                                     /* preferences              */
        return hit_prefs(x, y, changed);

    default:                                    /* NeoText                  */
        return hit_neotext(x, y, changed);
    }
}

/*
 * The click pass: which cell of the scene the pointer came down on,
 * and with which button -- 1 for the left, 2 for the right.  Returns
 * non-zero when the answer changed what should be on screen, which is
 * what tells the main loop to recomposite -- and the bands it left
 * behind are what tells the recomposite how little of it to do.
 *
 * The right button has one job: it opens the start menu *sticky*, so
 * the menu stays up while the left button goes on working underneath
 * it.  Only the right button again, or the orb, takes it away; nothing
 * else does, which is the whole point of asking for it.
 *
 * The order is the layer order: the right button first, because it
 * applies everywhere, then the orb that owns the menu, then the menu
 * itself before anything it covers, then whatever windows are open, and
 * only then the desktop.  A press that changes nothing -- wallpaper,
 * the gap between rows -- answers zero and costs no present.
 */
int nb_desktop_click(int x, int y, int btn)
{
    int i, cls = SEL_NONE, idx = -1, changed = 0;

    /* right button: the menu, sticky */
    if (btn == NB_BTN_R)
    {
        menu_open = !menu_open;
        menu_sticky = menu_open;
        band_menu();
        if (!menu_open && sel_kind == SEL_MENU)
            sel_kind = SEL_NONE;
        return 1;
    }

    /* start orb: drawn at (28,501) out to 11 px.  A control, not an
     * item -- one press opens the menu and one press closes it, and a
     * second press inside the window would only undo the first.  The
     * menu the orb opens is the ordinary, transient one. */
    if (nb_prefs.taskbar)
    {
        int dx = x - 28, dy = y - 501;

        if (dx * dx + dy * dy <= 121)
        {
            menu_open = !menu_open;
            menu_sticky = 0;
            band_menu();
            if (!menu_open && sel_kind == SEL_MENU)
                sel_kind = SEL_NONE;    /* the panel took the entry with it */
            return 1;
        }
    }

    if (menu_open)
    {
        for (i = 0; i < MENU_ITEMS; i++)
        {
            int ix = MENU_X + 6;
            int iy, ih;

            if (i < N_PLACES)
            {
                iy = MENU_P0 + i * MENU_PH;
                ih = MENU_PH;
            }
            else
            {
                iy = MENU_G0 + (i - N_PLACES) * MENU_ITEMH;
                ih = 30;
            }

            if (x >= ix && x < ix + MENU_ITEMW &&
                y >= iy && y < iy + ih)
            {
                cls = SEL_MENU;
                idx = i;
                break;
            }
        }
        if (cls == SEL_NONE && !menu_sticky)
        {
            menu_open = 0;              /* anywhere else dismisses it      */
            band_menu();
            if (sel_kind == SEL_MENU)
                sel_kind = SEL_NONE;    /* its light went with the panel   */
            changed = 1;
        }
        /* a sticky menu keeps standing over a press that missed it, and
         * that press goes on to the desktop underneath */
    }

    if (cls == SEL_NONE)
    {
        /* the panel's own controls, all one press: task buttons, then
         * the show-desktop slab on the very edge */
        if (nb_prefs.taskbar && y >= 490 && y < 512)
        {
            for (i = 1; i <= 6; i++)
            {
                struct task_cell t;

                if (!task_slot(i, &t) || x < t.x || x >= t.x + t.w)
                    continue;

                if      (i == 1) { files_open   = 0; band_files(1); }
                else if (i == 2) { clock_open   = 0; band_clock(); }
                else if (i == 3) { monitor_open = 0; band_monitor(); }
                else if (i == 4) { about_open   = 0; band_about(); }
                else if (i == 5) { prefs_open   = 0; band_prefs(1); }
                else             { neotext_open = 0; band_neotext(1); }
                if (focus_p == i)
                    focus_p = 0;
                changed |= sel_clear();
                return 1;
            }

            if (x >= DESK_X)
            {
                changed |= sel_clear();
                return show_desktop();
            }
        }

        /*
         * The windows, and the two gadgets standing in the corner they
         * are painted beside: asked in the order the draw pass paints
         * them, backwards -- the window with the keyboard first, then
         * the gadgets, then whatever those were painted over.  Two
         * windows can stand where another stands and a press landing on
         * both belongs to the one on top, so this list and scene() state
         * one z order from opposite ends of it.
         */
        {
            static const int seq[6] = { 3, 2, 6, 5, 1, 4 };
            int order[6], n = 0, r = 0, k;

            if (focus_p >= 1 && focus_p <= 6)
                order[n++] = focus_p;
            for (k = 0; k < 6; k++)
                if (seq[k] != focus_p)
                    order[n++] = seq[k];

            for (k = 0; k < n; k++)
            {
                r = win_press(order[k], x, y, &cls, &idx, &changed);
                if (r)
                    break;
            }
        }
    }

    /* one press names it, the next one runs it */
    if (cls != SEL_NONE)
    {
        unsigned dt = (unsigned)nb_fields - sel_t;
        int same = (cls == sel_kind && idx == sel_idx);

        if (same && dt <= DBL_FIELDS &&
            near(x, sel_x, DBL_SLOP) && near(y, sel_y, DBL_SLOP))
            return activate(cls, idx);

        if (same)                       /* the same item, but too late or */
            dbl_report(dt, x - sel_x, y - sel_y);   /* too far off: say why */

        changed |= sel_clear();         /* whatever was lit goes dark      */
        sel_kind = cls;
        sel_idx  = idx;
        sel_x    = x;
        sel_y    = y;
        sel_t    = (unsigned)nb_fields;
        band_select(cls, idx);
        return 1;
    }

    /* on the wallpaper, or in a gap: drop whatever was chosen */
    if (sel_clear())
        changed = 1;
    return changed;
}

/*
 * Where the lit item is, for the pass that follows: the point a press on
 * it would land on, and the field it was lit in.
 *
 * A key names an item exactly as a press does, so it has to leave the
 * double press where the press left it -- the window and the drift
 * nb_desktop_click measures against are read from here, and neither knows
 * about the keyboard unless this puts the answer in front of them.  The
 * geometry is the click pass's own, kept beside it: a menu place, a
 * program row, or a line of the browser's list.
 */
static void sel_mark(void)
{
    if (sel_kind == SEL_MENU)
    {
        int iy, ih;

        if (sel_idx < N_PLACES)
        {
            iy = MENU_P0 + sel_idx * MENU_PH;
            ih = MENU_PH;
        }
        else
        {
            iy = MENU_G0 + (sel_idx - N_PLACES) * MENU_ITEMH;
            ih = MENU_ITEMH;        /* the row's own pitch, as band_select */
        }
        sel_x = MENU_X + 6 + MENU_ITEMW / 2;
        sel_y = iy + ih / 2;
    }
    else if (sel_kind == SEL_ROW)
    {
        sel_x = FILES_X + FILES_W / 2;
        sel_y = FILES_Y + FILES_ROW0 + sel_idx * FILES_ROWH +
                FILES_ROWH / 2;
    }
    sel_t = (unsigned)nb_fields;
}

/*
 * The keyboard pass: the same scene, named by keys instead of by the
 * pointer.
 *
 * A key does what a press does and nothing else.  The two exist because
 * a pointer is not always the quickest way to say something, not because
 * they are two different scenes -- so the two Amiga keys open the start
 * menu the way the orb opens it, Escape puts down whatever is lit the way
 * a press on the wallpaper does, Up and Down walk the list that is
 * standing (the menu while it is up, the browser's rows when it is not)
 * and are a single press on whatever they land on, and Return runs what
 * is lit, which is the second.
 *
 * It answers in the same terms as nb_desktop_click -- non-zero when the
 * answer changed what should be on screen -- and takes its bands the same
 * way, which is what makes a key that lands where nothing is lit cost no
 * present, and the two safe to call in either order.
 */
int nb_desktop_key(int c)
{
    int count, kind, walk, from, down;

    /*
     * The two Amiga keys are the orb, one on either side of the
     * keyboard, and they open the ordinary transient menu.  The right
     * button's sticky menu is a gesture the keyboard has no way to make,
     * and two menus would be two ways of saying one thing.
     */
    if (c == NB_KEY_LAMIGA || c == NB_KEY_RAMIGA)
    {
        if (!nb_prefs.taskbar)
            return 0;

        menu_open = !menu_open;
        menu_sticky = 0;
        band_menu();
        if (!menu_open && sel_kind == SEL_MENU)
            sel_kind = SEL_NONE;    /* the panel took the entry with it */
        return 1;
    }

    if (c == NB_KEY_ESC)
    {
        if (menu_open)
        {
            menu_open = 0;          /* the menu first, as anywhere else   */
            band_menu();
            if (sel_kind == SEL_MENU)
                sel_kind = SEL_NONE;
            return 1;
        }

        if (sel_kind != SEL_NONE)
            return sel_clear();     /* what is lit, before what is open   */

        /*
         * Nothing is lit and no menu stands, so Escape puts down the
         * program that has the keyboard -- the cross does exactly this
         * for the pointer, and the keyboard should have the same answer
         * rather than a different one for the same wish.
         */
        switch (focus_p)
        {
        case 1: files_open   = 0; band_files(1);   break;
        case 2: clock_open   = 0; band_clock();    break;
        case 3: monitor_open = 0; band_monitor();  break;
        case 4: about_open   = 0; band_about();    break;
        case 5: prefs_open   = 0; band_prefs(1);   break;
        case 6: neotext_open = 0; band_neotext(1); break;
        default: return 0;           /* nothing has the keyboard          */
        }
        focus_p = 0;
        band_add(486, 512);          /* its button goes dark with it      */
        return 1;
    }

    /*
     * Return is the second press: the light already says which entry, so
     * this opens it through the very call a double press makes -- one
     * path to an activation, and nothing to keep in step.  With nothing
     * lit it is the reader's new line, but only when the reader is the
     * program being typed into.
     */
    if (c == NB_KEY_RET)
    {
        if (sel_kind != SEL_NONE)
            return activate(sel_kind, sel_idx);
        if (focus_p == 5)
            return prefs_key(c);
        if (focus_p == 6)
            return neotext_key(c);
        return 0;
    }

    /*
     * Everything else belongs to the reader while the reader has the
     * keyboard, menu or no menu: a menu is a list, and a list has no
     * letters in it.  Left, right, tab, backspace and the characters all
     * go through the same call the caret does.
     */
    if (focus_p == 6 && c != NB_KEY_UP && c != NB_KEY_DOWN)
        return neotext_key(c);

    /*
     * And the pane keeps the rest while it has the keyboard, for the
     * same reason: a swatch or a check box that only a pointer can work
     * is a preference half the machine cannot change.
     */
    if (focus_p == 5)
        return prefs_key(c);

    if (c != NB_KEY_UP && c != NB_KEY_DOWN)
        return 0;

    down = (c == NB_KEY_DOWN);

    if (menu_open)
    {
        count = MENU_ITEMS;
        kind  = SEL_MENU;
    }
    else if (focus_p == 6)
    {
        /* the reader keeps every arrow while it has the keyboard: a key
         * it cannot use is a key that did nothing, not one to hand to a
         * list standing behind it */
        if (!neotext_key(c))
            return 0;
        band_neotext(0);
        return 1;
    }
    else if (files_open)
    {
        count = row_count();
        kind  = SEL_ROW;
    }
    else
        return 0;                   /* neither list is standing           */

    if ((kind == SEL_MENU && sel_kind == SEL_MENU) ||
        (kind == SEL_ROW  && sel_kind == SEL_ROW))
        from = sel_idx;             /* the light is already in this list  */
    else
        from = -1;                  /* nothing lit: start at the top      */

    walk = (from >= 0) ? from : (down ? -1 : count);
    walk += down ? 1 : -1;
    if (walk < 0)
        walk = 0;
    else if (walk >= count)
        walk = count - 1;
    if (walk == from && from >= 0)
        return 0;                   /* against the end of the list        */

    sel_clear();
    sel_kind = kind;
    sel_idx  = walk;
    sel_mark();
    band_select(sel_kind, sel_idx);
    return 1;
}

/* ------------------------------------------------------------------ *
 * Gadgets
 * ------------------------------------------------------------------ */

/* outer, inner (26,18), inner (21,15) endpoint offsets for eight ticks
 * around a 30 px face -- the four cardinals run long and bright */
static const int dial_tick[8][4] = {
    {  0, -26,  0, -21 }, { 18, -18, 15, -15 },
    { 26,   0, 21,   0 }, { 18,  18, 15,  15 },
    {  0,  26,  0,  21 }, { -18, 18, -15, 15 },
    { -26,  0, -21,  0 }, { -18,-18, -15,-15 }
};

static void gadget_clock(int cx, int cy, int r)
{
    int i;

    gfx_disc(cx + 1, cy + 4, r + 3, C_WB_SHADE);      /* shade          */
    gfx_disc(cx, cy, r, C_WB_LINE);                   /* black rim      */
    gfx_disc(cx, cy, r - 2, C_INK);                   /* white face     */

    for (i = 0; i < 8; i++)
        gfx_line(cx + dial_tick[i][0], cy + dial_tick[i][1],
                 cx + dial_tick[i][2], cy + dial_tick[i][3],
                 (i & 1) ? C_WB_SHADE : C_WB_LINE);

    gfx_line(cx, cy, cx - 11, cy - 6, C_WB_LINE);     /* hour           */
    gfx_line(cx, cy, cx + 15, cy - 8, C_WB_LINE);     /* minute         */
    gfx_line(cx, cy, cx + 3, cy + 19, C_ACC);         /* second         */
    gfx_disc(cx, cy, 3, C_WB_LINE);
}

static void bar(int x, int y, int w, int fill)
{
    /* a sunken track: a shaded rim with the grey panel inside it, and
     * the brand teal filled in from the left as far as the figure runs */
    gfx_fill_r(x, y, w, 10, 4, C_WB_SHADE);
    gfx_fill_r(x + 1, y + 1, w - 2, 8, 3, C_WB_GREY);
    gfx_fill_r(x + 1, y + 1, fill, 8, 3, C_ACC);
    gfx_alpha(x + 1, y + 1, fill, 3, C_INK, 40);
}

/*
 * The system monitor.
 *
 * The RAM bar is not decoration: its full scale is the 136 MB of fast
 * RAM NeoBench is built for, and the white tick cut through it marks the
 * 128 MB floor it will not run below -- both figures taken straight out
 * of probe.h, so the gauge cannot come to disagree with the boot log's
 * tag colour about the same question: "is this machine above the floor,
 * and has it reached the preferred size".  The CPU bar keeps its old
 * fixed position: there is no load figure to report yet, only the model,
 * and the log already carries that.
 */
static void gadget_monitor(int x, int y, int w, int h)
{
    int tw = w - 56;
    int full = tw - 2;                         /* the track's inner width */
    int c1 = (tw >> 2) + (tw >> 3);            /* 37.5 %, shifts only     */
    int lo = (full * (int)NB_FAST_FLOOR) / (int)NB_FAST_PREFERRED;
    int c2 = (full * (int)nb_probe_fast_mb()) / (int)NB_FAST_PREFERRED;

    if (c2 > full)
        c2 = full;
    if (lo > full)
        lo = full;

    panel(x, y, w, h, "System");

    /*
     * Its caption in the same terms the windows wear.  The monitor is
     * a window like any other -- it has a button on the bar, it takes
     * the keyboard, and it can be picked up and carried now -- and a
     * Workbench-blue title over a pane of glass two windows away would
     * put two desktops in one corner of the screen.
     */
    gfx_alpha(x + 1, y + 1, w - 2, 19, C_AERO, 200);
    gfx_alpha(x + 3, y + 1, w - 6, 1, C_INK, 150);
    gfx_alpha(x + 1, y + 19, w - 2, 1, C_AERO_RIM, 96);
    text_d(x + 8, y + 6, "System", C_AERO_TXT);

    text_d(x + 8, y + 23, "CPU", C_MUTE);
    bar(x + 46, y + 21, tw, c1);

    text_d(x + 8, y + 35, "RAM", C_MUTE);
    bar(x + 46, y + 33, tw, c2);
    gfx_fill(x + 47 + lo, y + 33, 1, 10, C_WB_LINE); /* the 128 MB floor */
}

/* ------------------------------------------------------------------ *
 * Taskbar
 * ------------------------------------------------------------------ */

/*
 * One answer for both the bar and the menu the orb opens, so the two
 * can never disagree about what they are made of: "bar = classic" in
 * Config/screen.cfg takes the glass away, and what is left is the flat
 * Workbench field the desktop was drawn with before Aero existed.
 */
static int aero_on(void)
{
    return nb_prefs.bar_style != BAR_CLASSIC;
}

/*
 * The bar's field, in Aero's own terms: not a colour painted across the
 * bottom of the screen but the backdrop itself darkened by an alpha
 * that runs the whole width and eases down the twenty-two rows, so the
 * wash the wallpaper is carrying still shows through the bar and the
 * bar reads as a pane of tinted glass rather than as a grey plank.
 * "glass" in Config/screen.cfg is how much of that wash is let through:
 * zero paints the field solid, one hundred leaves almost nothing of it
 * hidden.  A lit hairline runs along the very top -- the edge every
 * glass panel has -- and the bottom two rows close the pane off against
 * the desktop below it.
 */
static void aero_field(int y, int h)
{
    int i;
    int op = 255 - (int)nb_prefs.bar_glass * 2;

    if (op < 40)
        op = 40;
    if (op > 255)
        op = 255;

    for (i = 0; i < h; i++)
    {
        /* the pane is glassiest along its top edge and most solid just
         * before the bottom, which is what puts the light where it is */
        int a = op - (i < 4 ? (4 - i) * 7 : 0) +
                     (i >= h - 4 ? 14 : 0);

        if (a < 16) a = 16;
        if (a > 255) a = 255;
        gfx_alpha(0, y + i, 640, 1, C_AERO, (uint8_t)a);
    }

    gfx_alpha(0, y,     640, 1, C_INK,     165);      /* the lit rim   */
    gfx_alpha(0, y + 1, 640, 1, C_INK,      74);
    gfx_alpha(0, y + 2, 640, 1, C_INK,      34);
    gfx_alpha(0, y + h - 2, 640, 1, C_AERO_RIM, 78);   /* its closing bead */
    gfx_alpha(0, y + h - 1, 640, 1, C_SHADOW, 205);
}

/*
 * A button on the glass: a rounded field of a slightly lighter blue
 * grey, a hairline rim round it and a line of light along its own top
 * edge.  The pressed state is the brighter one -- Aero lights the thing
 * it is showing as running rather than sinking it into the bar.
 */
static void aero_btn(int x, int y, int w, int h, int on)
{
    gfx_alpha_r(x, y, w, h, 4, on ? C_INK : C_AERO_RIM, on ? 150 : 120);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 3, on ? C_AERO_ACT : C_AERO_BTN,
                on ? 238 : 195);
    gfx_alpha(x + 2, y + 1, w - 4, 1, C_INK, on ? 130 : 78);
}

/*
 * The start orb: Workbench's launcher drawn as a bevelled disc -- a
 * hard shade under it, a white rim, the grey body and the light across
 * the top of it -- with NeoBench's mark on the pearl the artwork was
 * drawn for.  One control, so one press: the bevel describes the thing
 * rather than standing for a second state to press through.  On the
 * glass it becomes a dark pearl lit from above, and when the menu is
 * up it throws a halo of the brand teal, which is the whole of the
 * feedback the launcher gives that it has been opened.
 */
static void start_orb(void)
{
    const int cx = 28, cy = 501;

    if (aero_on())
    {
        if (menu_open)
        {
            gfx_disc_a(cx, cy, 15, C_ACC, 70);
            gfx_disc_a(cx, cy, 12, C_ACC, 96);
        }

        gfx_disc(cx + 1, cy + 2, 11, C_SHADOW);          /* shade     */
        gfx_disc(cx, cy, 11, menu_open ? C_INK : C_AERO_RIM);
        gfx_disc(cx, cy, 10, menu_open ? C_AERO_HI : C_AERO);
        gfx_disc_a(cx, cy - 3, 8, C_INK, menu_open ? 120 : 84);
        gfx_disc(cx, cy, 6, menu_open ? C_ACC : LOGO_BG);
        logo_mark(cx - 5, cy - 5, 10);
        return;
    }

    gfx_disc(cx + 1, cy + 2, 11, C_WB_SHADE);            /* shade     */
    gfx_disc(cx, cy, 11, C_INK);                         /* rim       */
    gfx_disc(cx, cy, 10, C_WB_GREY);                     /* body      */
    gfx_disc_a(cx, cy - 3, 8, C_INK, 70);                /* light     */
    gfx_disc(cx, cy, 6, LOGO_BG);                        /* pearl     */
    logo_mark(cx - 6, cy - 6, 12);                       /* the mark  */
}

/*
 * The pinned launchers: three Workbench buttons -- hard edge, grey
 * body, lit along the top and the left and shaded along the other two
 * -- each carrying the glyph of what it starts.  Geometry is the
 * panel's own: eighteen square, two apart, clear of the orb.
 *
 * On the glass the launcher *is* the icon, which is how Aero pins its
 * own: no button round it, only a tile of the set with the bar
 * showing through the drop under it.  The three take blue, rose and
 * green, the order the three glyphs have always been drawn in.
 */
static void quick_launch(void)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        int x = 58 + i * 22;

        if (aero_on())
        {
            static const uint16_t top[3] = {
                MUI_BLU_T, MUI_ROS_T, MUI_GRN_T
            };
            static const uint16_t bot[3] = {
                MUI_BLU_B, MUI_ROS_B, MUI_GRN_B
            };
            int px = x + 1;                      /* the 16 pixel tile */

            mui_plate(px, 493, 16, 4, top[i], bot[i]);

            if (i == 0)                          /* documents           */
            {
                gfx_fill_r(px + 4, 498, 8, 10, 1, C_INK);
                gfx_fill(px + 5, 500, 6, 1, MUI_CUT);
                gfx_fill(px + 5, 502, 6, 1, MUI_CUT);
                gfx_fill(px + 5, 504, 4, 1, MUI_CUT);
            }
            else if (i == 1)                     /* media               */
            {
                gfx_disc(px + 8, 503, 4, C_INK);
                gfx_tri(px + 6, 500, px + 6, 506, px + 11, 503, MUI_CUT);
            }
            else                                 /* telemetry           */
            {
                gfx_fill(px + 4, 505, 3, 3, C_INK);
                gfx_fill(px + 7, 502, 3, 6, C_INK);
                gfx_fill(px + 10, 499, 3, 9, C_INK);
            }
            continue;
        }

        gfx_fill(x, 492, 18, 18, C_WB_GREY);
        gfx_fill(x, 492, 18, 1, C_WB_LINE);
        gfx_fill(x, 509, 18, 1, C_WB_LINE);
        gfx_fill(x, 492, 1, 18, C_WB_LINE);
        gfx_fill(x + 17, 492, 1, 18, C_WB_LINE);
        gfx_fill(x + 1, 493, 16, 1, C_INK);
        gfx_fill(x + 1, 493, 1, 16, C_INK);
        gfx_fill(x + 1, 508, 16, 1, C_WB_SHADE);
        gfx_fill(x + 16, 493, 1, 16, C_WB_SHADE);
    }

    if (aero_on())
        return;

    gfx_fill_r(63, 496, 8, 10, 2, C_ACC);            /* documents      */
    gfx_fill(64, 498, 6, 1, C_WB_LINE);
    gfx_fill(64, 501, 6, 1, C_WB_LINE);

    gfx_disc(89, 501, 6, C_ACC);                     /* media          */
    gfx_tri(88, 497, 88, 505, 94, 501, C_INK);

    gfx_fill(106, 501, 3, 4, C_ACC);                 /* telemetry      */
    gfx_fill(110, 499, 3, 6, C_ACC);
    gfx_fill(114, 497, 3, 8, C_ACC);
}

/*
 * Where program `id' (1 Files .. 6 NeoText) has its button, or 0 when it
 * is not running or the row has run out of room.  The draw pass asks
 * for each id in turn; the click pass asks for the id under the
 * pointer.  One answer, one truth about where the buttons are.
 *
 * The row is the width it is and the bar does not run off its own end,
 * so the programs are given the room in the order they were started and
 * the ones that come last go without a button while there is none left
 * to give them -- four programs already out-run the row, and what is
 * standing is what the click pass can reach.
 */
static int task_slot(int id, struct task_cell *t)
{
    static const char *const nm[6] = {
        "Files", "Clock", "Monitor", "About", "Preferences", "NeoText"
    };
    int x = TASK_X0, i;

    for (i = 0; i < 6; i++)
    {
        struct task_cell c;
        int on;

        if      (i == 0) on = files_open;
        else if (i == 1) on = clock_open;
        else if (i == 2) on = monitor_open;
        else if (i == 3) on = about_open;
        else if (i == 4) on = prefs_open;
        else             on = neotext_open;
        if (!on)
            continue;

        c.name = nm[i];
        c.w = strw(c.name) + 24;
        if (x + c.w > TASK_X1)
            break;                      /* out of panel: leave it out    */

        c.x = x;
        if (i + 1 == id)
        {
            *t = c;
            return 1;
        }
        x += c.w + 4;
    }
    return 0;
}

/*
 * One button of the task manager.  In Workbench's terms: a grey
 * bevelled gadget while its program is running, reversed into the
 * Workbench blue while it is the one on top.  On the glass it is a
 * rounded pane like the pinned launchers, and the one on top is the
 * lit one -- Aero puts a glow behind the window you are in rather than
 * a field of a different colour under it.  The icon and the name keep
 * the places they had in either style, so the only thing that moves
 * between the two states is the field they are set on.
 */
static void task_btn(int x, int w, int active, const char *s)
{
    const int y = 494, h = 14;

    if (aero_on())
    {
        aero_btn(x, y, w, h, active);
        gfx_fill_r(x + 6, y + 3, 8, 8, 2,
                   active ? C_ACC : C_AERO_RIM);
        gfx_alpha(x + 7, y + 4, 6, 3, C_INK, 70);
        text_d(x + 18, y + 3, s, active ? C_INK : C_AERO_TXT);
        return;
    }

    if (active)
    {
        gfx_fill(x, y, w, h, C_WB_BLUE);
        gfx_fill(x, y, w, 1, C_INK);
        gfx_fill(x, y + h - 1, w, 1, C_WB_SHADE);
    }
    else
    {
        gfx_fill(x, y, w, h, C_WB_GREY);
        gfx_fill(x, y, w, 1, C_WB_LINE);
        gfx_fill(x, y + h - 1, w, 1, C_WB_LINE);
        gfx_fill(x, y, 1, h, C_WB_LINE);
        gfx_fill(x + w - 1, y, 1, h, C_WB_LINE);
        gfx_fill(x + 1, y + 1, w - 2, 1, C_INK);
        gfx_fill(x + 1, y + 1, 1, h - 2, C_INK);
    }

    gfx_fill_r(x + 6, y + 3, 8, 8, 2, active ? C_ACC : C_ACC_D);
    gfx_alpha(x + 7, y + 4, 6, 3, C_INK, 70);
    text_d(x + 18, y + 3, s, active ? C_INK : C_WB_LINE);
}

/* one button per program that is running, and none when none is */
static void task_manager(void)
{
    int i;

    for (i = 1; i <= 6; i++)
    {
        struct task_cell t;

        if (task_slot(i, &t))
            task_btn(t.x, t.w, focus_p == i, t.name);
    }
}

/*
 * The uptime readout, laid out where Plasma puts its clock: at the
 * right-hand end of the panel, past the tray, and left of the
 * show-desktop slab.  NeoBench has no real-time clock, so there is no
 * wall time this machine could honestly show -- it counts what it can
 * actually observe, fields, and reports the seconds those add up to.
 * The hours/minutes/seconds split is done by repeated subtraction:
 * dividing here would be the one divide in the file by a value the
 * compiler cannot fold away.
 */
static void uptime_text(void)
{
    unsigned s = nb_secs, h = 0, m = 0;
    char buf[16];
    int rx = DESK_X - 10;
    int lx;

    while (s >= 3600U) { s -= 3600U; h++; }
    while (s >= 60U)   { s -= 60U;   m++; }
    fmt_time(buf, h, m, s);

    if (aero_on())
    {
        /* Aero sets the clock in two: the value over the label, both
         * right-aligned to the sliver's edge, which is the whole of the
         * difference between it and the single line the Workbench bar
         * carries.  There is no date and no wall time to put there --
         * the machine has neither -- so the label says what the value
         * actually is rather than implying a clock it does not have. */
        text_right(rx, 493, buf, C_INK);
        text_right(rx, 503, "uptime", C_AERO_TXT);
        return;
    }

    lx = rx - strw(buf) - 24;
    if (lx < 546)                       /* never over the tray's rule     */
        lx = 546;
    text_d(lx, 497, "up", C_MUTE);
    text_right(rx, 497, buf, C_TEXT);
}

/*
 * The sliver on the extreme right of the panel: take the whole desktop,
 * or give it back.  In Workbench's terms it is a gadget -- bevelled grey
 * while the windows are up, reversed into the blue while they are down.
 * On the glass it runs the full height of the bar, separated from it by
 * a hairline of its own dark, and lights right through when the desktop
 * is what you are looking at.
 */
static void show_desktop_slab(int y, int h)
{
    const int sw = 640 - DESK_X;

    if (aero_on())
    {
        gfx_alpha(DESK_X, y, 1, h, C_SHADOW, 190);
        gfx_alpha(DESK_X + 1, y, sw - 1, h, C_AERO_RIM, 170);
        gfx_alpha(DESK_X + 2, y, sw - 3, 1, C_INK, desk_hidden ? 150 : 84);
        if (desk_hidden)
            gfx_alpha(DESK_X + 2, y + 2, sw - 3, h - 4, C_INK, 96);
        return;
    }

    {
        const int sy = y + 4, sh = h - 8;

        gfx_fill(DESK_X, sy, sw, sh, desk_hidden ? C_WB_BLUE : C_WB_GREY);
        gfx_fill(DESK_X, sy, sw, 1, desk_hidden ? C_INK : C_WB_LINE);
        gfx_fill(DESK_X, sy + sh - 1, sw, 1,
                 desk_hidden ? C_WB_SHADE : C_WB_LINE);
        gfx_fill(DESK_X, sy, 1, sh, desk_hidden ? C_INK : C_WB_LINE);
        gfx_fill(DESK_X + sw - 1, sy, 1, sh,
                 desk_hidden ? C_WB_SHADE : C_WB_LINE);
    }
}

/* a sunken rule in the panel: shaded, then lit, so it reads as a
 * groove cut into the grey rather than as a line drawn on it */
static void groove(int x, int y, int h)
{
    gfx_fill(x, y, 1, h, C_WB_SHADE);
    gfx_fill(x + 1, y, 1, h, C_INK);
}

/* the tray: a sunken well in the panel, with NeoBench's three bars
 * standing in it */
static void tray_well(void)
{
    if (aero_on())
    {
        /* no well on the glass -- a recess in a pane of glass reads as
         * a hole, so the tray is a slightly lighter rounded field with
         * the three bars in front of it instead */
        gfx_alpha_r(464, 493, 28, 16, 3, C_SHADOW, 96);
        gfx_alpha_r(465, 494, 26, 14, 3, C_AERO_BTN, 190);
        gfx_alpha(466, 494, 24, 1, C_INK, 60);

        gfx_fill(470, 501, 4, 4, C_ACC);                 /* tray              */
        gfx_fill(476, 499, 4, 6, C_ACC);
        gfx_fill(482, 497, 4, 8, C_ACC);
        return;
    }

    gfx_fill(464, 493, 28, 16, C_WB_GREY);
    gfx_fill(464, 493, 28, 1, C_WB_SHADE);
    gfx_fill(464, 493, 1, 16, C_WB_SHADE);
    gfx_fill(464, 508, 28, 1, C_INK);
    gfx_fill(491, 493, 1, 16, C_INK);

    gfx_fill(470, 501, 4, 4, C_ACC_D);                /* tray              */
    gfx_fill(476, 499, 4, 6, C_ACC);
    gfx_fill(482, 497, 4, 8, C_ACC);
}

/*
 * The panel.  In Workbench's terms: a flat grey field across the
 * bottom of the screen with a lit edge along the top of it and a hard
 * dark one along the bottom, and every gadget on it bevelled the same
 * way -- launcher, pinned launchers, rule, task manager, clear run,
 * tray, clock, slab.
 *
 * In Aero's terms the field is the backdrop seen through a pane of
 * tinted glass (aero_field()) and the gadgets on it are rounded fields
 * of a slightly lighter blue grey (aero_btn()), with no bevel and no
 * groove anywhere: glass has edges, not mouldings.  The furniture runs
 * left to right either way and keeps its coordinates, so every hit area
 * has the same shape in both styles; only how the field, and the things
 * on it, are painted differs.
 */
static void taskbar(void)
{
    const int y = 490, h = 22;
    int aero = aero_on();

    if (aero)
        aero_field(y, h);
    else
    {
        gfx_fill(0, y, 640, h, C_WB_GREY);
        gfx_fill(0, y, 640, 1, C_INK);
        gfx_fill(0, y + h - 1, 640, 1, C_WB_LINE);
    }

    start_orb();                                 /* application launcher  */
    quick_launch();                              /* pinned launchers      */

    if (!aero)
        groove(146, y + 4, 14);
    task_manager();

    if (aero)
    {
        /* a hairline where the Workbench bar cuts a groove: enough to
         * say where the tray begins without cutting the glass */
        gfx_alpha(444, y + 5, 1, 12, C_INK, 54);
        gfx_alpha(544, y + 5, 1, 12, C_INK, 54);
    }
    else
        groove(440, y + 4, 14);
    tray_well();

    if (!aero)
        groove(540, y + 4, 14);
    uptime_text();
    show_desktop_slab(y, h);
}

/* ------------------------------------------------------------------ *
 * Start menu
 * ------------------------------------------------------------------ */

/* the type of an entry that is not chosen: light on the glass, dark on
 * Workbench's grey, because the two fields are opposite */
static uint16_t menu_ink(void)
{
    return aero_on() ? C_AERO_TXT : C_WB_LINE;
}

/*
 * One place: the 24 pixel plate the desktop used to give it as an icon
 * down its left edge, with the name beside it instead of under it --
 * there is no wallpaper to set type on here -- and the Workbench blue
 * across the whole width of the row when it is the entry the next press
 * takes.  The plate is the icon it always was, so the places read
 * as the same marks they were on the desktop; Files, which never had
 * an icon down that edge because it is the thing that opened them,
 * gets the drawer front the rest of them are filed in.
 */
static void menu_place(int i, int sel)
{
    const int x = MENU_X + 6;
    const int y = MENU_P0 + i * MENU_PH;

    if (sel)
        gfx_fill(x, y, MENU_ITEMW, MENU_PH, C_WB_BLUE);

    place_icon(i, x + 3, y + 1);
    text_d(x + 34, y + 9, places[i].name, sel ? C_INK : menu_ink());
}

/*
 * One entry of the start menu's program section: type on the field, and
 * the chosen entry reversed -- blue field, white type -- across the
 * whole width of the entry, which is what says this is the entry the
 * next press runs.
 *
 * An entry whose program is already showing carries a lit dot at its
 * right-hand end, which is what makes the menu a list of what is
 * running as well as a list of what can be started: pressing an entry
 * toggles it, so the dot is also the way a gadget gets dismissed again.
 * The glyph is the tile the set draws everywhere -- here at 14 pixels --
 * and keeps its own hue whether the entry is chosen or not, because the
 * blue field behind it is what says that, not a second recolouring.
 * Nothing here is drawn until the orb asks for it.
 */
static void menu_item(int i, int slot, const char *label, int open, int sel)
{
    const int x = MENU_X + 6;
    const int y = MENU_G0 + i * MENU_ITEMH;
    uint16_t ring = aero_on() ? C_AERO_RIM : C_WB_LINE;

    if (sel)
        gfx_fill(x, y, MENU_ITEMW, 30, C_WB_BLUE);

    menu_glyph(slot, x + 8, y + 8);
    text_d(x + 30, y + 11, label, sel ? C_INK : menu_ink());

    if (open) {
        gfx_disc(x + MENU_ITEMW - 15, y + 15, 5, ring);
        gfx_disc(x + MENU_ITEMW - 15, y + 15, 4, C_INK);
        gfx_disc(x + MENU_ITEMW - 15, y + 15, 2, C_ACC);
    }
}

/*
 * The pane of glass the menu is cut from: a dark blue-grey laid over
 * the backdrop at a very high alpha, so a trace of the wash behind it
 * still comes through, with a lit rim along its top and left edges and
 * a soft drop of its own dark thrown to the right and down.  The Workbench
 * menu throws a fifty per cent checker instead -- see menu_shade().
 */
static void menu_body(void)
{
    const int x = MENU_X, y = MENU_Y, w = MENU_W, h = MENU_H;

    gfx_alpha_r(x, y, w, h, 5, C_AERO_RIM, 210);            /* the rim  */
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 4, C_AERO, 246);
    gfx_alpha(x + 3, y + 3, w - 6, 1, C_INK, 70);           /* top light */
    gfx_alpha(x + 3, y + 3, 1, h - 6, C_INK, 46);

    text_d(x + 8, y + 6, "NeoBench", C_AERO_TXT);
    gfx_fill(x + 2, y + 20, w - 4, 1, C_AERO_RIM);
}

/*
 * What is thrown behind the pane.  The Workbench menu throws a fifty
 * per cent checker to the right and down of its box, which is how that
 * desktop separates a menu from the backdrop; glass throws a smooth
 * shadow of its own dark instead, because a hard checker under a lit
 * rim reads as a second box rather than as depth.  Both go down before
 * the pane does.
 */
static void menu_shade(void)
{
    int x, y;

    if (aero_on())
    {
        gfx_alpha_r(MENU_X + 3, MENU_Y + 3, MENU_W, MENU_H, 5,
                    C_SHADOW, 110);
        return;
    }

    for (y = MENU_Y + 2; y < MENU_Y + MENU_H + 3; y++)
        for (x = MENU_X + MENU_W; x < MENU_X + MENU_W + 3; x++)
            if ((x + y) & 1)
                gfx_pixel(x, y, C_WB_SHADE);

    for (y = MENU_Y + MENU_H; y < MENU_Y + MENU_H + 3; y++)
        for (x = MENU_X + 2; x < MENU_X + MENU_W + 3; x++)
            if ((x + y) & 1)
                gfx_pixel(x, y, C_WB_SHADE);
}

/*
 * The whole menu: its pane, the places first, one rule, the programs.
 * The rule is the only thing that says where one section ends and the
 * other begins -- there are no headings, because a heading over five
 * entries the desktop used to carry as icons says only that they are
 * icons that moved.
 */
static void start_menu(void)
{
    int i;

    menu_shade();
    if (aero_on())
        menu_body();
    else
        panel(MENU_X, MENU_Y, MENU_W, MENU_H, "NeoBench");

    for (i = 0; i < N_PLACES; i++)
        menu_place(i, sel_kind == SEL_MENU && sel_idx == i);

    gfx_fill(MENU_X + 6, MENU_SEP, MENU_ITEMW, 1,
             aero_on() ? C_AERO_RIM : C_WB_LINE);

    for (i = 0; i < MENU_PROGS; i++)
    {
        static const char *nm[MENU_PROGS] = { "About" };
        int s = menu_slot[i];

        menu_item(i, s, nm[i], about_open,
                  sel_kind == SEL_MENU && sel_idx == N_PLACES + i);
    }
}

/* ------------------------------------------------------------------ *
 * Scene
 * ------------------------------------------------------------------ */

/*
 * Serial read-out of what the scene is currently showing -- the pointer
 * dump's twin, and the only way a log can say which of the desktop's
 * programs are running without a screen to look at.
 */
void nb_desktop_dump(void)
{
    static const char key[7] = { 'm', 'f', 'c', 'o', 'a', 'r', 'n' };
    const int val[7] = { menu_open, files_open, clock_open, monitor_open,
                         about_open, prefs_open, neotext_open };
    int i;

    amiga_serial_putc('>');
    amiga_serial_putc('u');
    amiga_serial_putc('i');
    amiga_serial_putc(' ');
    for (i = 0; i < 7; i++) {
        amiga_serial_putc(key[i]);
        amiga_serial_putc('=');
        amiga_serial_putc(val[i] ? '1' : '0');
        amiga_serial_putc(' ');
    }

    /* what the last press named, if it only named something: m or r for
     * a menu entry or a list row, and '-' for none.  The difference
     * between a select and an open is otherwise invisible in a log that
     * only carries the program flags, and the digit says which entry --
     * the first six are places, the last one a program.  t= is whether
     * the menu is the sticky one the right button opens, and foc= is the
     * program holding the keyboard -- 0 when none does, which is what
     * makes Escape a no-op rather than a surprise. */
    amiga_serial_putc('s');
    amiga_serial_putc('=');
    if (sel_kind == SEL_NONE)
        amiga_serial_putc('-');
    else {
        amiga_serial_putc(sel_kind == SEL_MENU ? 'm' : 'r');
        amiga_serial_putc((char)('0' + sel_idx));
    }
    amiga_serial_putc(' ');
    amiga_serial_putc('t');
    amiga_serial_putc('=');
    amiga_serial_putc(menu_sticky ? '1' : '0');
    amiga_serial_putc(' ');
    amiga_serial_putc('f');
    amiga_serial_putc('o');
    amiga_serial_putc('c');
    amiga_serial_putc('=');
    amiga_serial_putc((char)('0' + focus_p));
    amiga_serial_putc(' ');

    /*
     * Where the window holding the keyboard actually stands.  The
     * pointer's position says the pointer moved; only this says what
     * came with it, which is the whole of what can be read back from
     * a log about a drag -- and "-" when no window has the keyboard,
     * so that the field is always there to compare two lines by.
     * A window's corner can be off the left edge, because a window may
     * stand half off an edge, so the number is printed signed.
     */
    {
        char buf[24], *d;
        int wx, wy, ww, wh, n;

        d = put_str(buf, "at=");
        if (focus_p >= 1 && focus_p <= 6) {
            win_box(focus_p, &wx, &wy, &ww, &wh);
            n = wx;
            if (n < 0) {
                *d++ = '-';
                n = -n;
            }
            d = put_num(d, (unsigned)n);
            d = put_str(d, ",");
            n = wy;
            if (n < 0) {
                *d++ = '-';
                n = -n;
            }
            d = put_num(d, (unsigned)n);
        } else {
            d = put_str(d, "-");
        }
        *d++ = ' ';
        *d = '\0';
        for (d = buf; *d; d++)
            amiga_serial_putc(*d);
    }

    /* where the press landed, in screen pixels: without this a log can
     * say what was chosen but never what it was aiming at, and the
     * difference between "too late" and "next to it" is not guessable. */
    {
        char buf[20], *d;

        d = put_str(buf, "p=");
        d = put_num(d, (unsigned)nb_pointer_x());
        d = put_str(d, ",");
        d = put_num(d, (unsigned)nb_pointer_y());
        *d++ = ' ';
        *d = '\0';
        for (d = buf; *d; d++)
            amiga_serial_putc(*d);
    }

    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}

/*
 * The scene, in full: what one band of the band list below is asked to
 * rebuild.  Every branch is a pure function of the flags, so rebuilding
 * any slice of it again gives exactly the pixels the full pass put down
 * -- which is the whole reason a slice is enough.
 */
static void scene(void)
{
    wallpaper();

    /*
     * Programs, and only the ones the user has asked for.  The order
     * here is the z order, bottom first: the windows standing over the
     * wallpaper, then the two gadgets in the corner they are painted
     * beside, and then -- drawn once, and at the very top -- the program
     * that has the keyboard, so a window being used can never be half
     * covered by one that is not.  The click pass walks this list in
     * exact reverse, which is what makes a press that lands on two
     * windows belong to the one the eye sees on top of them.
     */
    if (about_open   && focus_p != 4) window_main();
    if (files_open   && focus_p != 1) window_files();
    if (prefs_open   && focus_p != 5) window_prefs();
    if (neotext_open && focus_p != 6) window_neotext();
    if (clock_open   && focus_p != 2) gadget_clock(CLOCK_X, CLOCK_Y, CLOCK_R);
    if (monitor_open && focus_p != 3) gadget_monitor(MON_X, MON_Y, MON_W, MON_H);

    if      (focus_p == 4 && about_open)   window_main();
    else if (focus_p == 1 && files_open)   window_files();
    else if (focus_p == 5 && prefs_open)   window_prefs();
    else if (focus_p == 6 && neotext_open) window_neotext();
    else if (focus_p == 2 && clock_open)   gadget_clock(CLOCK_X, CLOCK_Y, CLOCK_R);
    else if (focus_p == 3 && monitor_open) gadget_monitor(MON_X, MON_Y, MON_W, MON_H);

    if (nb_prefs.taskbar)
        taskbar();

    if (menu_open)
        start_menu();               /* over the bar's edge, never on it */
}

/*
 * Recomposite: rebuild what changed, then present only what the change
 * touched.  The click pass has already said which rows those are; the
 * first paint of the session has not, and claims all 512 of them.
 */
void nb_desktop_render(void)
{
    int i, y0, y1;

    if (band_n == 0)
        band_add(0, 512);

    for (i = 0; i < band_n; i++)
    {
        gfx_init();                 /* a clean arena for this band   */
        gfx_band(band_a[i], band_b[i]);
        scene();
        gfx_present();
        gfx_packed(&y0, &y1);       /* what the pack really rewrote  */
        nb_pointer_present_rows(y0, y1);
    }

    gfx_band_all();
    band_n = 0;
}
