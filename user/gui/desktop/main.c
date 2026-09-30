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
 * Nothing on the wallpaper at all -- the five places the desktop used
 * to show as a column of icons down its left edge are entries in the
 * start menu the orb opens, and so are the directory browser, the
 * clock, the monitor and the about panel.  Each program is a flag set
 * by that menu and tested in the draw pass below, so what is on screen
 * is exactly what the user asked for.  Config/screen.cfg carries
 * "bar = aero | classic" (the flat Workbench field the bar was drawn
 * with first) and "glass", how much of the backdrop the pane shows.
 */

#include "../../../boot/rom/gfx.h"
#include "../../../boot/rom/amiga.h"
#include "../../../boot/rom/kbd.h"
#include "../../../boot/rom/prefs.h"
#include "../../../boot/rom/pfs.h"
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
 * The backdrop, and this one is NeoBench's own: the white-to-mint wash
 * with three soft mint discs laid over it, then the hairline grid, and
 * the mark and the wordmark on top of all of it.  Nothing is sunk into
 * anything: against a light field the artwork carries itself, so it
 * goes down last and at full strength.  The wordmark is navy where it
 * used to be teal, because navy is what still reads as type on white --
 * the teal behind it is only its shadow now.
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

    gfx_vgrad(0, 0, 640, 512, nb_prefs.bg_top, nb_prefs.bg_bot);

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

/*
 * The program glyphs of the start menu -- the same tile at 14 pixels
 * with a mark that fits the eight rows the hairline leaves clear.
 * Files shares Core's blue, the clock takes Docs' amber, the monitor
 * Bench's green and About Home's violet, so every hue in the set is
 * the same hue wherever it turns up and none of them is invented for
 * a single place.
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
    default:                                         /* About  */
        mui_plate(x, y, 14, 4, MUI_VIO_T, MUI_VIO_B);
        gfx_disc(x + 7, y + 7, 4, C_INK);
        gfx_fill(x + 7, y + 4, 1, 1, MUI_CUT);       /* the i  */
        gfx_fill(x + 7, y + 6, 1, 3, MUI_CUT);
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
 */
#define N_PLACES 5

static const struct {
    const char *name;       /* what the entry says                     */
    const char *dir;        /* store directory it opens, "" = root     */
} places[N_PLACES] = {
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
    case 0:  icon_home(x, y);   break;
    case 1:  icon_screen(x, y); break;
    case 2:  icon_bench(x, y);  break;
    case 3:  icon_docs(x, y);   break;
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

/* caption button: 0 minimise (floor bar), 1 maximise (square),
 * 2 close (cross) -- a Workbench gadget, bevelled grey and set into
 * the blue of the title it belongs to */
static void cap_btn(int x, int y, int kind)
{
    gfx_fill(x, y, 13, 11, C_WB_GREY);
    gfx_fill(x, y, 13, 1, C_WB_LINE);
    gfx_fill(x, y + 10, 13, 1, C_WB_LINE);
    gfx_fill(x, y, 1, 11, C_WB_LINE);
    gfx_fill(x + 12, y, 1, 11, C_WB_LINE);
    gfx_fill(x + 1, y + 1, 11, 1, C_INK);
    gfx_fill(x + 1, y + 1, 1, 9, C_INK);

    if (kind == 2) {
        gfx_line(x + 4, y + 3, x + 9, y + 8, C_TEXT);
        gfx_line(x + 4, y + 8, x + 9, y + 3, C_TEXT);
    } else if (kind == 1) {
        gfx_fill(x + 4, y + 3, 6, 6, C_TEXT);
    } else {
        gfx_fill(x + 4, y + 7, 6, 2, C_TEXT);
    }
}

/*
 * Workbench's window: the frame above, a title reversed into the
 * Workbench blue along the top of it, the caption gadgets bevelled
 * into that blue, and a white field sunk into the middle for the
 * program to draw in.
 */
static void glass_window(int x, int y, int w, int h, int th,
                         const char *title, int nbtn)
{
    int i, bx;

    wb_frame(x, y, w, h);

    /* caption */
    gfx_fill(x + 2, y + 2, w - 4, th - 2, C_WB_BLUE);
    text_d(x + 14, y + (th - 8) / 2, title, C_INK);

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

static void window_main(void)
{
    const int x = 124, y = 40, w = 300, h = 100;

    glass_window(x, y, w, h, 20, "NeoBench", 3);

    text_d(x + 10, y + 28, "NeoBench 0.1.6", C_TEXT);
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
 * below has to land on exactly the rows the draw pass put down.
 */
#define FILES_X     376
#define FILES_Y     176
#define FILES_W     248
#define FILES_H     232
#define FILES_HEAD  28         /* "Name"/"Size" header row             */
#define FILES_ROW0  50         /* first list row, from the window top  */
#define FILES_ROWH  22         /* row pitch                            */
#define FILES_ROWS  6          /* ".." plus five children              */

static unsigned cur_dir;       /* the directory being shown; 0 = root  */

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
#define MENU_Y      180
#define MENU_W      196
#define MENU_H      306          /* MENU_Y + this stops short of the bar */
#define MENU_HEAD   22           /* entries start below the title rule   */
#define MENU_ITEMW  (MENU_W - 12)
#define MENU_PH     26           /* place row pitch: a 24 px plate, +1   */
#define MENU_ITEMH  34           /* program row pitch: 30 px box, 4 gap  */
#define MENU_PROGS  4
#define MENU_ITEMS  (N_PLACES + MENU_PROGS)

#define MENU_P0     (MENU_Y + MENU_HEAD + 4)             /* first place */
#define MENU_SEP    (MENU_P0 + N_PLACES * MENU_PH + 4)   /* section rule */
#define MENU_G0     (MENU_SEP + 4)                       /* first program */

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
static void band_about(void)    { band_add(36, 154); }
static void band_clock(void)    { band_add(44, 116); }
static void band_monitor(void)  { band_add(112, 177); }

/* The browser window, and -- when its task button comes or goes with
 * it -- the slice of the bar that carries that button. */
static void band_files(int bar)
{
    band_add(FILES_Y - 4, FILES_Y + FILES_H + 14);
    if (bar)
        band_add(486, 512);
}

/*
 * Every program window at once, for the one control that takes them
 * all down together: from the about panel's shadow at the top of the
 * raster to the browser's at the bottom, plus the buttons their closes
 * and opens leave behind on the bar.  The start menu is drawn over
 * that range too, and its entries carry the running dot, so it is
 * claimed as well whenever it is up.
 */
static void band_programs(void)
{
    band_add(36, 422);
    band_add(486, 512);
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

        ry = y + FILES_ROW0 + i * FILES_ROWH;
        if (child == PFS_NONE)
            break;

        nd = &nb_pfs_nodes[child];
        on = (sel_kind == SEL_ROW && sel_idx == i);
        row_light(i);
        row_glyph(nd->dir ? 1 : 2, x + 10, ry + 1);
        text_d(x + 30, ry + 3, nd->name, on ? C_INK : C_TEXT);
        if (nd->dir)
            col_r(x + w - 10, ry + 3, "<DIR>");
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

    gfx_fill(x + 10, y + 196, w - 20, 1, C_WB_SHADE);
    d = put_num(buf, count);
    d = put_str(d, count == 1u ? " object" : " objects");
    *d = '\0';
    text_d(x + 10, y + 204, buf, C_MUTE);
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

/*
 * Show desktop: the narrow slab on the right-hand end of the panel.
 *
 * One press takes every program window off the wallpaper at once, the
 * next puts them back exactly where they were -- the flags are the
 * whole of what a window is, so saving four bits saves the desktop.
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
        desk_hidden = 0;
    }
    else
    {
        desk_saved = (files_open ? 1 : 0) | (clock_open ? 2 : 0) |
                     (monitor_open ? 4 : 0) | (about_open ? 8 : 0);
        files_open = clock_open = monitor_open = about_open = 0;
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
        {
            int p = idx - N_PLACES;
            int on;

            if      (p == 0) { files_open   = !files_open;  band_files(1);  on = files_open; }
            else if (p == 1) { clock_open   = !clock_open;  band_clock();   on = clock_open; }
            else if (p == 2) { monitor_open = !monitor_open; band_monitor(); on = monitor_open; }
            else             { about_open   = !about_open;  band_about();   on = about_open; }

            if (on)
                focus_p = p + 1;        /* the panel shows it as active   */
            else if (focus_p == p + 1)
                focus_p = 0;
        }
    }
    else                                   /* a list row: step into it    */
    {
        unsigned t = row_target(idx);

        if (t != PFS_NONE && t != cur_dir)
        {
            cur_dir = t;
            band_files(0);
            changed = 1;
        }
    }

    return changed;
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
            for (i = 1; i <= 4; i++)
            {
                struct task_cell t;

                if (!task_slot(i, &t) || x < t.x || x >= t.x + t.w)
                    continue;

                if      (i == 1) { files_open   = 0; band_files(1); }
                else if (i == 2) { clock_open   = 0; band_clock(); }
                else if (i == 3) { monitor_open = 0; band_monitor(); }
                else             { about_open   = 0; band_about(); }
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

        /* a close is one press, always: nobody double clicks a cross */
        if (about_open && on_close(x, y, 124, 40, 300))
        {
            about_open = 0;
            if (focus_p == 4)
                focus_p = 0;
            band_about();
            return 1;
        }

        if (files_open && on_close(x, y, FILES_X, FILES_Y, FILES_W))
        {
            files_open = 0;
            if (focus_p == 1)
                focus_p = 0;
            changed |= sel_clear();
            band_files(1);
            return 1;
        }

        if (files_open &&
            x >= FILES_X && x < FILES_X + FILES_W &&
            y >= FILES_Y && y < FILES_Y + FILES_H)
        {
            for (i = 0; i < FILES_ROWS; i++)
            {
                int ry = FILES_Y + FILES_ROW0 + i * FILES_ROWH;

                if (y >= ry && y < ry + FILES_ROWH)
                {
                    cls = SEL_ROW;
                    idx = i;
                    break;
                }
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
        if (!menu_open)
            return sel_clear();     /* on the wallpaper: drop the choice */

        menu_open = 0;              /* the menu first, as anywhere else   */
        band_menu();
        if (sel_kind == SEL_MENU)
            sel_kind = SEL_NONE;
        return 1;
    }

    /*
     * Return is the second press: the light already says which entry, so
     * this opens it through the very call a double press makes -- one
     * path to an activation, and nothing to keep in step.
     */
    if (c == NB_KEY_RET)
    {
        if (sel_kind == SEL_NONE)
            return 0;               /* nothing is lit: the key is nothing */
        return activate(sel_kind, sel_idx);
    }

    if (c != NB_KEY_UP && c != NB_KEY_DOWN)
        return 0;

    down = (c == NB_KEY_DOWN);

    if (menu_open)
    {
        count = MENU_ITEMS;
        kind  = SEL_MENU;
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
 * Where program `id' (1 Files .. 4 About) has its button, or 0 when it
 * is not running or the row has run out of room.  The draw pass asks
 * for each id in turn; the click pass asks for the id under the
 * pointer.  One answer, one truth about where the buttons are.
 */
static int task_slot(int id, struct task_cell *t)
{
    int x = TASK_X0, i;

    for (i = 0; i < 4; i++)
    {
        struct task_cell c;
        int w;

        c.name = (i == 0) ? "Files" : (i == 1) ? "Clock" :
                 (i == 2) ? "Monitor" : "About";
        if      (i == 0) { if (!files_open)   continue; }
        else if (i == 1) { if (!clock_open)   continue; }
        else if (i == 2) { if (!monitor_open) continue; }
        else             { if (!about_open)   continue; }

        w = strw(c.name) + 24;
        if (x + w > TASK_X1)
            break;                      /* out of panel: leave it out    */

        c.x = x;
        c.w = w;
        if (i + 1 == id)
        {
            *t = c;
            return 1;
        }
        x += w + 4;
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

    for (i = 1; i <= 4; i++)
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
 * takes.  The plate is the icon it always was, so the five places read
 * as the same five marks they were on the desktop.
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
static void menu_item(int i, const char *label, int open, int sel)
{
    const int x = MENU_X + 6;
    const int y = MENU_G0 + i * MENU_ITEMH;
    uint16_t ring = aero_on() ? C_AERO_RIM : C_WB_LINE;

    if (sel)
        gfx_fill(x, y, MENU_ITEMW, 30, C_WB_BLUE);

    menu_glyph(i, x + 8, y + 8);
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
        static const char *nm[MENU_PROGS] = { "Files", "Clock", "Monitor",
                                              "About" };
        int on = (i == 0) ? files_open :
                 (i == 1) ? clock_open :
                 (i == 2) ? monitor_open : about_open;

        menu_item(i, nm[i], on,
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
    static const char key[5] = { 'm', 'f', 'c', 'o', 'a' };
    const int val[5] = { menu_open, files_open, clock_open, monitor_open,
                         about_open };
    int i;

    amiga_serial_putc('>');
    amiga_serial_putc('u');
    amiga_serial_putc('i');
    amiga_serial_putc(' ');
    for (i = 0; i < 5; i++) {
        amiga_serial_putc(key[i]);
        amiga_serial_putc('=');
        amiga_serial_putc(val[i] ? '1' : '0');
        amiga_serial_putc(' ');
    }

    /* what the last press named, if it only named something: m or r for
     * a menu entry or a list row, and '-' for none.  The difference
     * between a select and an open is otherwise invisible in a log that
     * only carries the program flags, and the digit says which entry --
     * the first five are places, the last four programs.  t= is whether
     * the menu is the sticky one the right button opens. */
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

    /* programs, and only the ones the user has asked for */
    if (about_open)
        window_main();
    if (files_open)
        window_files();
    if (clock_open)
        gadget_clock(590, 76, 30);
    if (monitor_open)
        gadget_monitor(452, 116, 172, 48);

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
