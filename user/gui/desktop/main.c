/*
 * nb_desktop_render() - static Vista Aero themed scene for the AGA
 * target (640x256 hires, 256 colours).
 *
 * Composited into the RGB565 back buffer with the gfx primitives:
 *   - aurora wallpaper (gradients + soft glow discs)
 *   - two glass windows: software alpha over a pre-blurred wallpaper,
 *     1 px light border, frosted content pane, Aero caption buttons
 *   - dark glass taskbar with start orb, quick launch, task buttons
 *     and clock
 *   - sidebar gadgets: analog clock + notes
 * Finally quantised and pushed to the planar frame buffer.
 */

#include "../../../boot/rom/gfx.h"
#include "../../../boot/rom/amiga.h"

/* ---- palette ---------------------------------------------------- */
#define C_WHITE      NB_RGB(31, 63, 31)
#define C_TITLE_TXT  NB_RGB(3, 8, 16)      /* dark navy, on light glass   */
#define C_BODY_TXT   NB_RGB(8, 20, 30)
#define C_MUTED      NB_RGB(14, 32, 31)
#define C_GLASS_TINT NB_RGB(22, 54, 31)    /* pale blue frosted tint      */
#define C_PANE       NB_RGB(27, 58, 31)    /* frosted white content pane  */
#define C_LINE       NB_RGB(18, 40, 31)
#define C_GREEN      NB_RGB(6, 54, 8)

/* ---- helpers ---------------------------------------------------- */

static void text_shadow(int x, int y, const char *s, uint16_t c)
{
    gfx_text(x + 1, y + 1, s, NB_RGB(0, 2, 6));
    gfx_text(x, y, s, c);
}

/* frosted caption button: 17x15 at x,y; kind 0 = min, 1 = max, 2 = close */
static void cap_btn(int x, int y, int kind)
{
    if (kind == 2) {
        gfx_fill_r(x, y, 17, 15, 3, NB_RGB(16, 6, 6));
        gfx_alpha_r(x, y, 17, 15, 3, NB_RGB(31, 10, 9), 190);
        gfx_line(x + 5, y + 4, x + 11, y + 10, C_WHITE);
        gfx_line(x + 5, y + 10, x + 11, y + 4, C_WHITE);
    } else {
        gfx_alpha_r(x, y, 17, 15, 3, C_WHITE, 96);
        if (kind == 0) {                       /* minimise */
            gfx_fill(x + 5, y + 9, 7, 2, NB_RGB(4, 12, 22));
        } else {                               /* maximise */
            gfx_fill(x + 4, y + 4, 9, 1, NB_RGB(4, 12, 22));
            gfx_fill(x + 4, y + 5, 1, 6, NB_RGB(4, 12, 22));
            gfx_fill(x + 12, y + 5, 1, 6, NB_RGB(4, 12, 22));
            gfx_fill(x + 4, y + 10, 9, 1, NB_RGB(4, 12, 22));
            gfx_fill(x + 5, y + 6, 7, 3, NB_RGB(10, 30, 31));
        }
    }
}

/* Aero glass window: blurred backdrop, tinted alpha, light border,
 * brighter caption strip, frosted content pane. */
static void glass_window(int x, int y, int w, int h, int th,
                         const char *title, int buttons)
{
    int i;

    gfx_blur(x, y, w, h);
    gfx_alpha_r(x, y, w, h, 7, C_WHITE, 176);              /* rim */
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 6, C_GLASS_TINT, 110);
    /* caption light strip, inset past the corner radius */
    gfx_alpha(x + 8, y + 2, w - 16, th - 3, C_WHITE, 44);
    /* frosted content pane */
    gfx_alpha_r(x + 2, y + th, w - 4, h - th - 2, 5, C_PANE, 240);

    text_shadow(x + 10, y + (th - 8) / 2, title, C_TITLE_TXT);
    for (i = 0; i < buttons; i++) {
        int bx = x + w - (20 + i * 19);
        cap_btn(bx, y + 3, (i == 0) ? 2 : (i == 1 ? 1 : 0));
    }
}

/* ---- scene pieces ----------------------------------------------- */

static void wallpaper(void)
{
    gfx_vgrad(0, 0, 640, 132, NB_RGB(5, 16, 31), NB_RGB(7, 45, 31));
    gfx_vgrad(0, 132, 640, 124, NB_RGB(7, 45, 31), NB_RGB(2, 8, 17));
    gfx_disc_a(470, 74, 196, NB_RGB(24, 52, 30), 44);      /* teal aurora */
    gfx_disc_a(430, 60, 120, NB_RGB(26, 58, 31), 34);
    gfx_disc_a(150, 214, 140, NB_RGB(6, 34, 30), 40);      /* low glow    */
    gfx_disc_a(200, 96, 90, NB_RGB(4, 22, 26), 30);
}

static void desktop_icon(int x, int y, uint16_t badge, const char *label)
{
    gfx_fill_r(x, y, 30, 26, 5, NB_RGB(10, 26, 31));
    gfx_alpha_r(x, y, 30, 26, 5, C_WHITE, 200);
    gfx_fill(x + 5, y + 6, 20, 13, NB_RGB(3, 12, 24));
    gfx_fill(x + 7, y + 8, 16, 9, badge);
    gfx_fill(x + 12, y + 20, 6, 2, NB_RGB(12, 30, 31));
    text_shadow(x - 6, y + 30, label, C_WHITE);
}

static void window_files(void)
{
    static const uint16_t badges[5] = {
        NB_RGB(8, 40, 31), NB_RGB(6, 54, 8),
        NB_RGB(29, 56, 6), NB_RGB(20, 24, 31), NB_RGB(31, 20, 18)
    };
    static const char *names[5] = {
        "Kernel", "Bootrom", "Docs", "Tools", "Media"
    };
    static const char *sizes[5] = {
        "1.2 MB", "512 kB", "-", "-", "-"
    };
    int i;
    const int x = 390, y = 64, w = 168, h = 150, th = 22;

    glass_window(x, y, w, h, th, "Files", 1);

    /* column header */
    gfx_alpha(x + 4, y + th + 2, w - 8, 12, NB_RGB(20, 44, 31), 40);
    gfx_text(x + 12, y + th + 4, "Name", C_MUTED);
    gfx_text(x + w - 58, y + th + 4, "Size", C_MUTED);
    gfx_fill(x + 4, y + th + 15, w - 8, 1, C_LINE);

    for (i = 0; i < 5; i++) {
        int ry = y + th + 18 + i * 18;
        if (i == 1)                              /* hover row */
            gfx_alpha(x + 4, ry, w - 8, 17, NB_RGB(10, 40, 31), 34);
        gfx_fill_r(x + 10, ry + 3, 13, 13, 3, badges[i]);
        gfx_alpha(x + 12, ry + 5, 5, 4, C_WHITE, 150);
        gfx_text(x + 30, ry + 5, names[i], C_BODY_TXT);
        gfx_text(x + w - 60, ry + 5, sizes[i], C_MUTED);
    }
    gfx_fill(x + 4, y + h - 18, w - 8, 1, C_LINE);
    gfx_text(x + 12, y + h - 14, "5 objects", C_MUTED);
}

static void window_main(void)
{
    const int x = 60, y = 34, w = 340, h = 180, th = 24;

    glass_window(x, y, w, h, th, "NeoBench", 3);

    gfx_text(x + 14, y + th + 8, "Welcome to NeoBench", NB_RGB(2, 14, 30));
    gfx_text(x + 14, y + th + 20, "Aero desktop on original AGA hardware",
             C_MUTED);
    gfx_fill(x + 14, y + th + 34, w - 28, 1, C_LINE);

    /* status rows */
    gfx_fill_r(x + 14, y + th + 44, 7, 7, 3, C_GREEN);
    gfx_text(x + 28, y + th + 44, "AGA 640x256, 256 colours", C_BODY_TXT);
    gfx_fill_r(x + 14, y + th + 58, 7, 7, 3, C_GREEN);
    gfx_text(x + 28, y + th + 58, "Motorola 68060 + FPU", C_BODY_TXT);
    gfx_fill_r(x + 14, y + th + 72, 7, 7, 3, C_GREEN);
    gfx_text(x + 28, y + th + 72, "10 MB memory, 800 KB floppy", C_BODY_TXT);

    gfx_text(x + 14, y + th + 92, "chipset: AGA   rom: AmigaOS 3.2.3",
             C_MUTED);

    /* About button */
    gfx_fill_r(x + w - 92, y + h - 32, 78, 22, 5, NB_RGB(14, 34, 31));
    gfx_alpha_r(x + w - 92, y + h - 32, 78, 22, 5, C_WHITE, 210);
    gfx_alpha_r(x + w - 91, y + h - 31, 76, 20, 4, NB_RGB(24, 56, 31), 60);
    gfx_text(x + w - 74, y + h - 26, "About", NB_RGB(2, 12, 24));
}

static void start_orb(void)
{
    const int cx = 26, cy = 240;

    gfx_disc(cx, cy, 15, NB_RGB(0, 3, 8));                /* shadow    */
    gfx_disc(cx, cy, 14, NB_RGB(5, 22, 31));              /* rim       */
    gfx_disc(cx, cy, 12, NB_RGB(8, 38, 31));              /* bowl      */
    gfx_disc(cx, cy, 10, NB_RGB(15, 56, 31));             /* core      */
    gfx_disc_a(cx, cy - 5, 8, C_WHITE, 78);               /* gloss     */
    /* four-pane flag */
    gfx_fill(cx - 5, cy - 5, 5, 5, C_WHITE);
    gfx_fill(cx + 1, cy - 5, 5, 5, C_WHITE);
    gfx_fill(cx - 5, cy + 1, 5, 5, C_WHITE);
    gfx_fill(cx + 1, cy + 1, 5, 5, C_WHITE);
}

static void taskbar(void)
{
    const int y = 226, h = 30;
    int i;

    gfx_blur(0, y, 640, h);
    gfx_alpha(0, y, 640, h, NB_RGB(0, 0, 0), 150);
    gfx_alpha(0, y, 640, 1, C_WHITE, 120);               /* top highlight */
    gfx_alpha(0, y + 1, 640, 1, NB_RGB(0, 0, 0), 90);

    start_orb();

    /* quick launch */
    for (i = 0; i < 3; i++)
        gfx_alpha_r(52 + i * 24, y + 6, 19, 19, 4, C_WHITE, 64);
    gfx_fill_r(56, y + 10, 11, 11, 2, NB_RGB(10, 44, 31));
    gfx_fill_r(58, y + 12, 5, 5, 1, NB_RGB(26, 58, 31));
    gfx_fill_r(80, y + 10, 11, 11, 2, NB_RGB(10, 44, 31));
    gfx_fill_r(82, y + 12, 5, 5, 1, NB_RGB(31, 56, 6));
    gfx_fill_r(104, y + 10, 11, 11, 2, NB_RGB(10, 44, 31));
    gfx_fill_r(106, y + 12, 5, 5, 1, NB_RGB(31, 22, 18));
    gfx_fill(130, y + 5, 1, 20, NB_RGB(18, 40, 31));

    /* task buttons */
    gfx_alpha_r(142, y + 4, 124, 22, 5, C_WHITE, 70);
    gfx_alpha_r(143, y + 5, 122, 20, 4, NB_RGB(14, 48, 31), 46);
    gfx_fill_r(150, y + 8, 15, 14, 3, NB_RGB(6, 30, 31));
    gfx_fill(153, y + 11, 9, 6, NB_RGB(24, 56, 31));
    gfx_text(172, y + 10, "NeoBench", C_WHITE);

    gfx_alpha_r(274, y + 4, 116, 22, 5, C_WHITE, 46);
    gfx_fill_r(282, y + 8, 15, 14, 3, NB_RGB(6, 30, 31));
    gfx_fill(285, y + 11, 9, 6, NB_RGB(29, 56, 6));
    gfx_text(304, y + 10, "Files", NB_RGB(26, 54, 31));

    /* tray + clock (right edge: clock text ends at 636) */
    gfx_alpha_r(530, y + 5, 24, 20, 4, C_WHITE, 40);
    gfx_fill_r(536, y + 10, 8, 8, 2, NB_RGB(6, 46, 31));
    gfx_alpha_r(556, y + 5, 24, 20, 4, C_WHITE, 40);
    gfx_fill_r(562, y + 10, 8, 8, 2, NB_RGB(6, 54, 8));
    gfx_fill(586, y + 5, 1, 20, NB_RGB(18, 40, 31));
    gfx_text(592, y + 6, "10:08", C_WHITE);
    gfx_text(592, y + 15, "26 Sep", NB_RGB(22, 48, 31));
}

static void gadget_clock(void)
{
    /* integer positions for 12 hours on a radius-25 circle */
    static const int tick[12][2] = {
        { 0, -25 }, { 12, -21 }, { 21, -12 }, { 25, 0 },
        { 21, 12 }, { 12, 21 }, { 0, 25 }, { -12, 21 },
        { -21, 12 }, { -25, 0 }, { -21, -12 }, { -12, -21 }
    };
    const int cx = 596, cy = 62;
    int i;

    gfx_blur(cx - 34, cy - 34, 68, 68);
    gfx_disc(cx, cy, 31, NB_RGB(14, 36, 31));             /* rim       */
    gfx_disc(cx, cy, 29, NB_RGB(27, 58, 31));             /* face      */
    gfx_disc_a(cx, cy - 10, 20, C_WHITE, 40);             /* glass     */

    for (i = 0; i < 12; i++) {
        int tx = cx + tick[i][0];
        int ty = cy + tick[i][1];
        if (i % 3 == 0)
            gfx_fill(tx - 1, ty - 1, 3, 3, NB_RGB(4, 16, 26));
        else
            gfx_fill(tx, ty, 1, 1, NB_RGB(10, 30, 31));
    }

    /* 10:08 hands: hour ~304 deg, minute ~48 deg from 12 o'clock */
    gfx_line(cx, cy, cx - 13, cy - 9, NB_RGB(3, 14, 26));
    gfx_line(cx, cy + 1, cx - 13, cy - 8, NB_RGB(3, 14, 26));
    gfx_line(cx, cy, cx + 16, cy - 15, NB_RGB(3, 14, 26));
    gfx_line(cx, cy + 1, cx + 16, cy - 14, NB_RGB(3, 14, 26));
    gfx_disc(cx, cy, 3, NB_RGB(31, 10, 9));
}

static void gadget_notes(void)
{
    const int x = 562, y = 104, w = 68, h = 64;

    gfx_blur(x, y, w, h);
    gfx_alpha_r(x, y, w, h, 6, C_WHITE, 170);
    gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 5, C_PANE, 224);
    gfx_text(x + 8, y + 7, "Notes", C_TITLE_TXT);
    gfx_fill(x + 6, y + 19, w - 12, 1, C_LINE);
    gfx_fill(x + 6, y + 26, w - 12, 1, NB_RGB(20, 44, 31));
    gfx_fill(x + 6, y + 35, w - 20, 1, NB_RGB(20, 44, 31));
    gfx_fill(x + 6, y + 44, w - 14, 1, NB_RGB(20, 44, 31));
    gfx_fill(x + 6, y + 53, w - 24, 1, NB_RGB(20, 44, 31));
}

/* ---- entry point ------------------------------------------------ */

void nb_desktop_render(void)
{
    gfx_init();
    amiga_serial_putc('w');

    wallpaper();
    desktop_icon(16, 14, NB_RGB(10, 44, 31), "System");
    desktop_icon(16, 74, NB_RGB(29, 56, 6), "Docs");
    amiga_serial_putc('p');

    window_files();
    window_main();
    amiga_serial_putc('n');

    gadget_clock();
    gadget_notes();
    amiga_serial_putc('g');

    taskbar();
    amiga_serial_putc('t');

    gfx_present();
    amiga_serial_putc('P');
}
