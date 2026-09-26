/*
 * nb_desktop_render() -- the NeoBench "futuristic clean" desktop.
 *
 * AGA, 640x512 interlaced, 256 colours (boot/rom/amiga.h).  Everything
 * below is composited into the RGB565 back buffer with the gfx
 * primitives and then quantised and packed by gfx_present(): there is no
 * bitmap anywhere in the ROM.
 *
 * Freestanding m68k rules apply throughout -- the link has no
 * __udivsi3, so no divide by a runtime value and no floating point.
 * Every division here is by a constant the compiler turns into a shift,
 * and the two numbers that have to move (bar widths, the uptime) are
 * made by shifting or by subtracting instead.
 *
 * The scene, top to bottom:
 *
 *   wallpaper  navy -> near-black vertical gradient, the NeoBench mark
 *              sunk into it as a watermark, three teal horizon glows and
 *              a 32 px hairline grid laid over the top of it
 *   icons      four custom tiles down the left edge -- chip, telemetry,
 *              layered docs, media badge -- each on a frosted rounded
 *              tile with a shadowed label under it
 *   windows    two dark glass panels: the wallpaper blurred in place
 *              beneath a blue-black tint, a one-pixel lit rim, a caption
 *              rule and small round caption buttons
 *   gadgets    an analog dial and a two-bar system monitor
 *   taskbar    a 44 px bar carrying the mark-only start orb, quick
 *              launch, task buttons, an activity indicator and the
 *              uptime readout
 */

#include "../../../boot/rom/gfx.h"
#include "../../../boot/rom/amiga.h"
#include "logo.h"

/* boot/rom/probe.c: how many megabytes of expansion space answered. */
extern unsigned nb_probe_mem_mb(void);

/* ------------------------------------------------------------------ *
 * Palette
 * ------------------------------------------------------------------ */
#define C_INK     NB_RGB(31, 63, 31)     /* white                       */
#define C_TEXT    NB_RGB(26, 56, 30)     /* bright body text            */
#define C_MUTE    NB_RGB(14, 32, 18)     /* secondary text              */
#define C_ACC     NB_RGB(6, 41, 22)      /* brand teal                  */
#define C_ACC_D   NB_RGB(3, 22, 13)      /* dim teal                    */
#define C_GREEN   NB_RGB(6, 54, 8)       /* status green                */
#define C_TILE    NB_RGB(2, 7, 8)        /* glass body                  */
#define C_PANE    NB_RGB(3, 11, 12)      /* inner pane / tracks         */
#define C_EDGE    NB_RGB(7, 22, 16)      /* inner rules                 */
#define C_RIM     NB_RGB(15, 36, 18)     /* lit rim                     */
#define C_RIM_D   NB_RGB(8, 24, 14)      /* dim rim                     */
#define C_SHADOW  NB_RGB(0, 1, 1)        /* drop shadow                 */
#define C_BG0     NB_RGB(2, 14, 8)       /* wallpaper top               */
#define C_BG1     NB_RGB(0, 2, 3)        /* wallpaper bottom            */
#define C_GLOW_A  NB_RGB(3, 30, 16)      /* teal horizon glow           */
#define C_GLOW_B  NB_RGB(4, 20, 14)      /* steel glow                  */
#define C_GLOW_C  NB_RGB(5, 34, 20)      /* cyan glow                   */
#define C_GRID    NB_RGB(10, 26, 16)     /* hairline grid               */

#define WM_X      106                    /* watermark veil box          */
#define WM_Y      225
#define WM_W      220
#define WM_H      231
#define WM_FADE   166                    /* veil strength: the mark shows
                                            at ~35 % underneath it, which
                                            is where teal still separates
                                            from a navy wallpaper        */

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

/* text with a one-pixel dark drop, so labels stay readable over the
 * wallpaper's glows as well as over glass */
static void text_d(int x, int y, const char *s, uint16_t c)
{
    gfx_text(x + 1, y + 1, s, C_SHADOW);
    gfx_text(x, y, s, c);
}

static void text_right(int right, int y, const char *s, uint16_t c)
{
    text_d(right - strw(s), y, s, c);
}

/* centred label under an icon tile */
static void label_c(int x, int y, int w, const char *s)
{
    int t = (w - strw(s)) / 2;

    text_d(x + t, y, s, C_TEXT);
}

static uint16_t bb_at(int x, int y)
{
    return *(const volatile uint16_t *)(uintptr_t)(
        NB_BACKBUF_BASE + (unsigned)(y * 640 + x) * 2u);
}

/* ------------------------------------------------------------------ *
 * Wallpaper
 * ------------------------------------------------------------------ */

/*
 * The watermark cannot be faded with a rectangle: a wash across the
 * mark's bounding box also washes the wallpaper under it, and against a
 * gradient that edge is plainly visible.  So each row of the veil is
 * filled with the wallpaper colour *of that row*, read back out of the
 * gradient before the mark went down.  Blending a colour with itself is
 * exact, so the background outside the artwork comes out untouched and
 * only the artwork is pulled back -- no edge anywhere.
 *
 * The glows and the grid go on afterwards, over mark and wallpaper
 * alike, for the same reason.
 */
static void wallpaper(void)
{
    int i;

    gfx_vgrad(0, 0, 640, 512, C_BG0, C_BG1);

    logo_mark(110, 210, 210);
    gfx_text_s(111, 429, "NEOBENCH", LOGO_NAVY, 3);
    gfx_text_s(110, 428, "NEOBENCH", LOGO_TEAL, 3);
    for (i = WM_Y; i < WM_Y + WM_H; i++)
        gfx_alpha(WM_X, i, WM_W, 1, bb_at(40, i), WM_FADE);

    gfx_disc_a(300, 452, 250, C_GLOW_A, 34);
    gfx_disc_a(556, 476, 210, C_GLOW_B, 40);
    gfx_disc_a(596, 58, 170, C_GLOW_C, 44);

    for (i = 0; i < 512; i += 32)
        gfx_alpha(0, i, 640, 1, C_GRID, 26);
    for (i = 16; i < 640; i += 32)
        gfx_alpha(i, 0, 1, 512, C_GRID, 15);
}

/* ------------------------------------------------------------------ *
 * Icons -- four custom marks, all built from the same primitives the
 * NeoBench logo is, so the set reads as one family.
 * ------------------------------------------------------------------ */

static void icon_tile(int x, int y)
{
    gfx_alpha_r(x + 2, y + 6, 44, 44, 11, C_SHADOW, 130);
    gfx_fill_r(x, y, 48, 48, 11, C_RIM);
    gfx_fill_r(x + 1, y + 1, 46, 46, 10, C_TILE);
    gfx_alpha_r(x + 1, y + 1, 46, 46, 10, C_ACC, 46);
    gfx_alpha(x + 5, y + 4, 38, 11, C_INK, 26);
}

/* System: an AGA gate array, legs out on all four sides */
static void icon_chip(int x, int y)
{
    int i;

    icon_tile(x, y);
    gfx_fill_r(x + 13, y + 13, 22, 22, 4, C_PANE);
    gfx_fill_r(x + 15, y + 15, 18, 18, 3, C_ACC);
    gfx_fill_r(x + 19, y + 19, 10, 10, 2, C_TILE);
    for (i = 0; i < 3; i++) {
        gfx_fill(x + 17 + i * 6, y + 8, 3, 5, C_ACC);
        gfx_fill(x + 17 + i * 6, y + 35, 3, 5, C_ACC);
        gfx_fill(x + 8, y + 17 + i * 6, 5, 3, C_ACC);
        gfx_fill(x + 35, y + 17 + i * 6, 5, 3, C_ACC);
    }
}

/* Bench: a rising bar chart under a threshold rule */
static void icon_bench(int x, int y)
{
    icon_tile(x, y);
    gfx_fill(x + 10, y + 37, 30, 2, C_INK);
    gfx_fill(x + 12, y + 30, 5, 7, C_ACC);
    gfx_fill(x + 19, y + 24, 5, 13, C_ACC);
    gfx_fill(x + 26, y + 17, 5, 20, C_ACC);
    gfx_fill(x + 33, y + 11, 5, 26, C_ACC);
    gfx_alpha(x + 10, y + 21, 30, 1, C_INK, 165);
}

/* Docs: three stacked cards, the front one lit */
static void icon_docs(int x, int y)
{
    icon_tile(x, y);
    gfx_fill_r(x + 9, y + 12, 22, 16, 3, C_PANE);
    gfx_fill_r(x + 13, y + 17, 22, 16, 3, C_EDGE);
    gfx_fill_r(x + 17, y + 22, 22, 15, 3, C_ACC);
    gfx_fill(x + 21, y + 26, 14, 2, C_TILE);
    gfx_fill(x + 21, y + 30, 10, 2, C_TILE);
}

/* Media: a play badge */
static void icon_media(int x, int y)
{
    icon_tile(x, y);
    gfx_disc(x + 24, y + 24, 15, C_ACC);
    gfx_disc(x + 24, y + 24, 12, C_PANE);
    gfx_tri(x + 21, y + 17, x + 21, y + 31, x + 33, y + 24, C_INK);
}

static void icons(void)
{
    const int x = 20;

    icon_chip(x, 36);    label_c(x, 90, 48, "System");
    icon_bench(x, 144);  label_c(x, 198, 48, "Bench");
    icon_docs(x, 252);   label_c(x, 306, 48, "Docs");
    icon_media(x, 360);  label_c(x, 414, 48, "Media");
}

/* ------------------------------------------------------------------ *
 * Windows
 * ------------------------------------------------------------------ */

/* caption button: 0 minimise (floor bar), 1 maximise (square),
 * 2 close (cross) */
static void cap_btn(int x, int y, int kind)
{
    gfx_fill_r(x, y, 13, 11, 3, C_RIM_D);
    gfx_alpha_r(x + 1, y + 1, 11, 9, 2, C_TILE, 205);
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
 * Dark glass: the wallpaper underneath is blurred first, then tinted
 * blue-black, so the panel keeps the colour of what is behind it without
 * carrying any of its detail -- the Vista read, without the chrome.
 */
static void glass_window(int x, int y, int w, int h, int th,
                         const char *title, int nbtn)
{
    int i, bx;

    gfx_alpha_r(x - 4, y + 6, w + 10, h + 4, 12, C_SHADOW, 120);
    gfx_blur(x + 2, y + 2, w - 4, h - 4);

    gfx_fill_r(x, y, w, h, 9, C_RIM);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 8, C_TILE, 210);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 8, C_ACC, 26);
    gfx_alpha(x + 3, y + 3, w - 6, 3, C_INK, 34);

    /* caption */
    gfx_fill(x + 1, y + th, w - 2, 1, C_EDGE);
    gfx_alpha(x + 4, y + 5, 3, th - 9, C_ACC, 235);
    text_d(x + 14, y + (th - 8) / 2, title, C_TEXT);

    bx = x + w - 8 - nbtn * 15;
    for (i = 0; i < nbtn; i++)
        cap_btn(bx + i * 15, y + (th - 11) / 2,
                (i == nbtn - 1) ? 2 : i);

    /* content pane */
    gfx_alpha_r(x + 4, y + th + 4, w - 8, h - th - 8, 7, C_PANE, 120);
    gfx_fill(x + 4, y + th + 3, w - 8, 1, C_EDGE);
}

/* a plain panel, for the gadgets */
static void panel(int x, int y, int w, int h, const char *title)
{
    gfx_alpha_r(x - 4, y + 5, w + 8, h + 4, 12, C_SHADOW, 120);
    gfx_blur(x + 2, y + 2, w - 4, h - 4);

    gfx_fill_r(x, y, w, h, 9, C_RIM);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 8, C_TILE, 214);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 8, C_ACC, 26);
    gfx_alpha(x + 3, y + 3, w - 6, 3, C_INK, 34);

    if (title) {
        gfx_fill(x + 1, y + 16, w - 2, 1, C_EDGE);
        gfx_alpha(x + 6, y + 5, 3, 7, C_ACC, 235);
        text_d(x + 14, y + 5, title, C_MUTE);
    }
}

static void status_row(int x, int y, const char *s)
{
    gfx_fill(x, y, 2, 8, C_ACC);
    text_d(x + 10, y, s, C_TEXT);
}

static void window_main(void)
{
    const int x = 124, y = 40, w = 300, h = 156;
    char mem[40];
    char *d;

    glass_window(x, y, w, h, 20, "NeoBench", 3);

    text_d(x + 10, y + 28, "NeoBench 0.1.0", C_TEXT);
    text_d(x + 10, y + 42, "Futuristic desktop on AGA", C_MUTE);
    gfx_alpha(x + 10, y + 56, w - 20, 1, C_EDGE, 220);

    status_row(x + 10, y + 66, "640x512 interlaced, 256 colours");
    status_row(x + 10, y + 82, "Motorola 68060 with FPU");

    d = put_num(mem, nb_probe_mem_mb());
    d = put_str(d, " MB memory, 2 MB chip");
    *d = '\0';
    status_row(x + 10, y + 98, mem);

    status_row(x + 10, y + 114, "AmigaOS 3.2.3 ROM chainload");
}

/* right-aligned column helper */
static void col_r(int right, int y, const char *s)
{
    text_d(right - strw(s), y, s, C_MUTE);
}

static void window_files(void)
{
    const int x = 376, y = 176, w = 248, h = 232;
    static const char *name[6] = {
        "Kickstart", "Workbench", "NeoBench",
        "bench", "docs", "startup-seq"
    };
    static const char *size[6] = {
        "512 KB", "1.2 MB", "512 KB", "24 KB", "40 KB", "1 KB"
    };
    int i, ry;

    glass_window(x, y, w, h, 20, "Files", 3);

    text_d(x + 10, y + 28, "Name", C_MUTE);
    col_r(x + w - 10, y + 28, "Size");
    gfx_alpha(x + 10, y + 40, w - 20, 1, C_EDGE, 220);

    for (i = 0; i < 6; i++) {
        ry = y + 50 + i * 22;
        gfx_fill_r(x + 10, ry + 1, 12, 12, 3,
                   i < 3 ? C_ACC : C_ACC_D);
        gfx_alpha(x + 11, ry + 2, 10, 4, C_INK, 70);
        text_d(x + 30, ry + 3, name[i], C_TEXT);
        col_r(x + w - 10, ry + 3, size[i]);
    }

    gfx_alpha(x + 10, y + 196, w - 20, 1, C_EDGE, 220);
    text_d(x + 10, y + 204, "6 objects", C_MUTE);
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

    gfx_disc(cx + 1, cy + 4, r + 3, C_SHADOW);
    gfx_disc(cx, cy, r, C_RIM);
    gfx_disc(cx, cy, r - 2, C_TILE);
    gfx_disc_a(cx, cy, r - 3, C_ACC, 30);
    gfx_alpha(cx - 7, cy - r + 5, 14, 4, C_INK, 44);

    for (i = 0; i < 8; i++)
        gfx_line(cx + dial_tick[i][0], cy + dial_tick[i][1],
                 cx + dial_tick[i][2], cy + dial_tick[i][3],
                 (i & 1) ? C_ACC : C_TEXT);

    gfx_line(cx, cy, cx - 11, cy - 6, C_TEXT);      /* hour           */
    gfx_line(cx, cy, cx + 15, cy - 8, C_TEXT);      /* minute         */
    gfx_line(cx, cy, cx + 3, cy + 19, C_ACC);       /* second         */
    gfx_disc(cx, cy, 3, C_INK);
}

static void bar(int x, int y, int w, int fill)
{
    gfx_fill_r(x, y, w, 10, 4, C_SHADOW);
    gfx_fill_r(x + 1, y + 1, w - 2, 8, 3, C_PANE);
    gfx_fill_r(x + 1, y + 1, fill, 8, 3, C_ACC);
    gfx_alpha(x + 1, y + 1, fill, 3, C_INK, 40);
}

static void gadget_monitor(int x, int y, int w, int h)
{
    int tw = w - 56;
    int c1 = (tw >> 2) + (tw >> 3);     /* 37.5 %, shifts only          */
    int c2 = (tw >> 1) + (tw >> 3);     /* 62.5 %                       */

    panel(x, y, w, h, "System");

    text_d(x + 8, y + 23, "CPU", C_MUTE);
    bar(x + 46, y + 21, tw, c1);

    text_d(x + 8, y + 35, "RAM", C_MUTE);
    bar(x + 46, y + 33, tw, c2);
}

/* ------------------------------------------------------------------ *
 * Taskbar
 * ------------------------------------------------------------------ */

static void start_orb(void)
{
    const int cx = 28, cy = 490;

    gfx_disc(cx, cy + 3, 21, C_SHADOW);
    gfx_disc(cx, cy, 19, C_RIM_D);
    gfx_disc(cx, cy, 18, LOGO_NAVY);
    gfx_disc(cx, cy, 17, LOGO_BG);
    logo_mark(cx - 12, cy - 12, 24);
    gfx_disc_a(cx, cy + 9, 16, C_ACC, 60);
}

static void quick_launch(void)
{
    const int y = 479;
    int i;

    for (i = 0; i < 3; i++)
        gfx_alpha_r(58 + i * 26, y, 22, 22, 6, C_INK, 18);

    gfx_fill_r(64, y + 5, 10, 13, 2, C_ACC);         /* documents      */
    gfx_fill(66, y + 8, 6, 1, C_TILE);
    gfx_fill(66, y + 11, 6, 1, C_TILE);

    gfx_disc(95, y + 11, 7, C_ACC);                  /* media          */
    gfx_tri(93, y + 7, 93, y + 15, 100, y + 11, C_TILE);

    gfx_fill(116, y + 12, 3, 6, C_ACC);              /* telemetry      */
    gfx_fill(121, y + 8, 3, 10, C_ACC);
    gfx_fill(126, y + 4, 3, 14, C_ACC);
}

static void task_btn(int x, int w, int active, const char *s)
{
    const int y = 474, h = 28;

    gfx_alpha_r(x, y, w, h, 7, C_INK, active ? 34 : 14);
    gfx_fill_r(x, y + 5, 2, h - 10, 1, active ? C_ACC : C_RIM_D);
    gfx_fill_r(x + 10, y + 8, 12, 12, 3, active ? C_ACC : C_ACC_D);
    gfx_alpha(x + 11, y + 9, 10, 4, C_INK, 70);
    text_d(x + 30, y + 10, s, active ? C_TEXT : C_MUTE);
}

/*
 * The uptime readout.  NeoBench has no real-time clock, so there is no
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

    while (s >= 3600U) { s -= 3600U; h++; }
    while (s >= 60U)   { s -= 60U;   m++; }
    fmt_time(buf, h, m, s);

    text_d(554, 486, "up ", C_MUTE);
    text_right(634, 486, buf, C_TEXT);
}

static void taskbar(void)
{
    const int y = 468, h = 44;

    gfx_blur(0, y, 640, h);
    gfx_alpha(0, y, 640, h, NB_RGB(0, 3, 6), 188);
    gfx_alpha(0, y, 640, 1, C_ACC, 170);
    gfx_alpha(0, y + h - 1, 640, 1, C_INK, 26);

    start_orb();
    quick_launch();

    gfx_alpha(146, y + 9, 1, 26, C_RIM_D, 190);
    task_btn(158, 124, 1, "NeoBench");
    task_btn(290, 76, 0, "Files");

    gfx_alpha(440, y + 9, 1, 26, C_RIM_D, 190);
    gfx_fill(470, 491, 4, 6, C_ACC_D);
    gfx_fill(476, 487, 4, 10, C_ACC);
    gfx_fill(482, 483, 4, 14, C_ACC);

    gfx_alpha(540, y + 9, 1, 26, C_RIM_D, 190);
    uptime_text();
}

/* ------------------------------------------------------------------ *
 * Scene
 * ------------------------------------------------------------------ */

void nb_desktop_render(void)
{
    gfx_init();

    wallpaper();
    icons();

    window_main();
    window_files();

    gadget_clock(590, 76, 30);
    gadget_monitor(452, 116, 172, 48);

    taskbar();

    gfx_present();
}
