/*
 * The NeoBench pointer: position, shape and the frame-borrowing overlay.
 *
 * The overlay works by remembering what the eight bitplanes held under
 * it.  amiga_fb_get() reads a pixel back as the colour index that was
 * there, the index goes into a small save buffer, and moving the pointer
 * writes those indices straight back before drawing itself somewhere
 * else.  Nothing has to be recomputed and no palette lookup is involved
 * in an undo -- the restoration is exact, which is what lets the
 * pointer sit on top of a window border or a line of text and leave no
 * trace when it moves away.
 *
 * Only the two colours the pointer is *drawn* with are picked out of the
 * running palette: the fill is the palette entry nearest to the colour
 * in pointer.cfg, the outline is the entry nearest to black.  Both are
 * re-picked whenever gfx_present() uploads a new palette, so the pointer
 * stays the brightest thing on screen without touching an entry the
 * scene is using.
 */
#include <stdint.h>
#include "amiga.h"
#include "prefs.h"
#include "pointer.h"

#define PTR_MAX_W   48            /* largest overlay, outline included   */
#define PTR_MAX_H   48
#define SCREEN_W    640
#define SCREEN_H    512

/* ------------------------------------------------------------------ *
 * Shapes: '.' is transparent, anything else is fill.
 * hx,hy is the hot cell -- the shape pixel that sits on the mouse.
 * ------------------------------------------------------------------ */

static const char *const arrow_rows[] = {
    "X..........",
    "XX.........",
    "X.X........",
    "X..X.......",
    "X...X......",
    "X....X.....",
    "X.....X....",
    "X......X...",
    "X.......X..",
    "X........X.",
    "X.....XXXXX",
    "X..X..X....",
    "X.X.X..X...",
    "XX..X..X...",
    "X....X..X..",
    ".....X..X..",
};

static const char *const cross_rows[] = {
    ".....X.....",
    ".....X.....",
    ".....X.....",
    ".....X.....",
    ".....X.....",
    "XXXXXXXXXXX",
    ".....X.....",
    ".....X.....",
    ".....X.....",
    ".....X.....",
    ".....X.....",
};

static const char *const ibeam_rows[] = {
    "XX.......XX",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    "..XXXXXXX..",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    ".X.......X.",
    "XX.......XX",
};

static const char *const dot_rows[] = {
    "..XXX..",
    ".XXXXX.",
    "XXXXXXX",
    "XXXXXXX",
    "XXXXXXX",
    ".XXXXX.",
    "..XXX..",
};

struct ptr_shape
{
    const char *const *rows;
    int w, h;
    int hx, hy;
};

static const struct ptr_shape shapes[] = {
    { arrow_rows, 11, 16, 0, 0 },          /* NB_PTR_ARROW */
    { cross_rows, 11, 11, 5, 5 },          /* NB_PTR_CROSS */
    { ibeam_rows, 11, 15, 5, 7 },          /* NB_PTR_IBEAM */
    { dot_rows,    7,  7, 3, 3 },          /* NB_PTR_DOT   */
};

/* ------------------------------------------------------------------ *
 * State
 * ------------------------------------------------------------------ */

static int mx, my;                 /* hot spot, screen pixels           */
static int ov_x, ov_y, ov_w, ov_h; /* overlay rect currently borrowed   */
static int have_ov;
static int last_x, last_y, last_w, last_h;   /* rect last drawn         */
static uint8_t save[PTR_MAX_W * PTR_MAX_H];
static int btn_prev;
static int fg_idx, bg_idx;
static unsigned seen_epoch;
static int shown;                  /* bss -- .data would be in ROM */

static const struct ptr_shape *cur_shape(void)
{
    unsigned s = (unsigned)nb_prefs.shape;

    if (s >= sizeof(shapes) / sizeof(shapes[0]))
        s = 0;
    return &shapes[s];
}

/* ------------------------------------------------------------------ *
 * Palette picking
 * ------------------------------------------------------------------ */

static int rgb565_r(uint16_t c)   { return ((c >> 11) & 31) * 255 / 31; }
static int rgb565_g(uint16_t c)   { return ((c >> 5) & 63) * 255 / 63; }
static int rgb565_b(uint16_t c)   { return (c & 31) * 255 / 31; }

/*
 * Nearest palette entry to an RGB565 colour.
 *
 * Division by the small constants is folded to shifts; the search itself
 * is 256 comparisons and only runs when the palette has actually been
 * re-uploaded, so it costs nothing per frame.
 */
static int nearest(uint16_t want)
{
    int tr = rgb565_r(want), tg = rgb565_g(want), tb = rgb565_b(want);
    int best = 0, best_d = 0x7fffffff;
    unsigned i;

    for (i = 0; i < 256; i++)
    {
        uint8_t r, g, b;
        int dr, dg, db, d;

        amiga_get_color(i, &r, &g, &b);
        dr = (int)r - tr;
        dg = ((int)g - tg) * 2;             /* green carries two more bits */
        db = (int)b - tb;
        d = dr * dr + dg * dg + db * db;
        if (d < best_d)
        {
            best_d = d;
            best = (int)i;
        }
    }
    return best;
}

static void pick_colours(void)
{
    unsigned epoch = amiga_pal_epoch();

    if (epoch == seen_epoch)
        return;
    seen_epoch = epoch;
    fg_idx = nearest(nb_prefs.colour);
    bg_idx = nearest(0x0000u);
}

/* ------------------------------------------------------------------ *
 * Borrowing the frame
 * ------------------------------------------------------------------ */

static void overlay_restore(void)
{
    int i, n;

    if (!have_ov)
        return;

    n = 0;
    for (i = 0; i < ov_h; i++)
    {
        int x;

        for (x = 0; x < ov_w; x++)
            amiga_fb_pixel((uint32_t)(ov_x + x), (uint32_t)(ov_y + i),
                           save[n++]);
    }
    have_ov = 0;
}

/*
 * Hand back the borrowed rows in [r0, r1) only.
 *
 * A partial present rewrites part of the frame and leaves the rest of
 * it standing, so part of the borrow is still the truth and part of it
 * is already history: writing the whole of it back would stamp an old
 * frame over a new one, and dropping it whole would leave the pointer's
 * untouched rows stranded on screen as a ghost.
 */
static void overlay_restore_rows(int r0, int r1)
{
    int i;

    if (!have_ov)
        return;
    if (r0 < ov_y)
        r0 = ov_y;
    if (r1 > ov_y + ov_h)
        r1 = ov_y + ov_h;

    for (i = r0; i < r1; i++)
    {
        int x;
        int n = (i - ov_y) * ov_w;

        for (x = 0; x < ov_w; x++)
            amiga_fb_pixel((uint32_t)(ov_x + x), (uint32_t)i,
                           save[n + x]);
    }
}

static void overlay_borrow(int x, int y, int w, int h)
{
    int i, n = 0;

    if (w <= 0 || h <= 0)
        return;

    for (i = 0; i < h; i++)
    {
        int x0;

        for (x0 = 0; x0 < w; x0++)
            save[n++] = amiga_fb_get((uint32_t)(x + x0), (uint32_t)(y + i));
    }
    ov_x = x;
    ov_y = y;
    ov_w = w;
    ov_h = h;
    have_ov = 1;
}

/* ------------------------------------------------------------------ *
 * Drawing
 * ------------------------------------------------------------------ */

static int cell_filled(const struct ptr_shape *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h)
        return 0;
    return s->rows[y][x] != '.';
}

static void cell_block(int px, int py, int scale, uint8_t idx)
{
    int a, b;

    for (b = 0; b < scale; b++)
        for (a = 0; a < scale; a++)
            amiga_fb_pixel((uint32_t)(px + a), (uint32_t)(py + b), idx);
}

/*
 * Cover rect for the shape: outline reaches one cell past the bitmap on
 * every side, and the shadow another cell down and right.
 */
static void shape_rect(const struct ptr_shape *s, int *rx, int *ry,
                       int *rw, int *rh)
{
    int scale = nb_prefs.scale;
    int x0 = mx + (-1 - s->hx) * scale;
    int y0 = my + (-1 - s->hy) * scale;

    *rx = x0;
    *ry = y0;
    *rw = (s->w + 3) * scale;
    *rh = (s->h + 3) * scale;
}

static void shape_draw(const struct ptr_shape *s, int scale)
{
    int x, y;

    /* shadow: one cell down and right of every fill pixel */
    if (nb_prefs.shadow)
        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->w; x++)
                if (cell_filled(s, x, y))
                    cell_block(mx + (x + 1 - s->hx) * scale,
                               my + (y + 1 - s->hy) * scale, scale, bg_idx);

    /* fill */
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            if (cell_filled(s, x, y))
                cell_block(mx + (x - s->hx) * scale,
                           my + (y - s->hy) * scale, scale, fg_idx);

    /* outline: empty cells touching a filled one, one cell all round */
    for (y = -1; y <= s->h; y++)
        for (x = -1; x <= s->w; x++)
            if (!cell_filled(s, x, y) &&
                (cell_filled(s, x - 1, y) || cell_filled(s, x + 1, y) ||
                 cell_filled(s, x, y - 1) || cell_filled(s, x, y + 1)))
                cell_block(mx + (x - s->hx) * scale,
                           my + (y - s->hy) * scale, scale, bg_idx);
}

static void overlay_draw(void)
{
    const struct ptr_shape *s = cur_shape();
    int scale = nb_prefs.scale;
    int x, y, w, h;

    if (!shown || !nb_prefs.visible)
        return;
    if (scale != 1 && scale != 2)
        scale = 1;

    pick_colours();
    shape_rect(s, &x, &y, &w, &h);

    /* clip the borrowed rect to the screen: everything drawn has to be
     * inside it, or the restore would miss a pixel */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W)  w = SCREEN_W - x;
    if (y + h > SCREEN_H)  h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;

    last_x = x;
    last_y = y;
    last_w = w;
    last_h = h;

    overlay_borrow(x, y, w, h);
    shape_draw(s, scale);
}

/* ------------------------------------------------------------------ *
 * Public interface
 * ------------------------------------------------------------------ */

void nb_pointer_enable(void)
{
    mx = nb_prefs.start_x;
    my = nb_prefs.start_y;
    btn_prev = 0;
    shown = nb_prefs.visible;
    overlay_draw();
}

void nb_pointer_after_present(void)
{
    /*
     * nb_pointer_present_rows() has already had its say: if the borrow
     * survived, the overlay's pixels are still sitting on the frame and
     * drawing again would only put a second copy on top of a picture
     * that is already right.  If it did not survive, the rows that
     * carried the overlay have gone with it and it has to go down
     * again.
     */
    if (!have_ov)
        overlay_draw();
}

void nb_pointer_present_rows(int y0, int y1)
{
    if (!have_ov)
        return;
    if (ov_y >= y1 || ov_y + ov_h <= y0)
        return;                          /* the band passed it by       */

    if (ov_y < y0)
        overlay_restore_rows(ov_y, y0);  /* above the band: still valid */
    if (ov_y + ov_h > y1)
        overlay_restore_rows(y1, ov_y + ov_h);
    have_ov = 0;
}

void nb_pointer_show(int show)
{
    if (!!show == !!shown)
        return;
    overlay_restore();
    shown = show;
    if (shown)
        overlay_draw();
}

int nb_pointer_x(void)
{
    return mx;
}

int nb_pointer_y(void)
{
    return my;
}

int nb_pointer_frame(void)
{
    int dx, dy, btn;
    int clicked = 0;
    int nx, ny;

    amiga_mouse_poll(&dx, &dy, &btn);

    if (dx || dy)
    {
        int speed = nb_prefs.speed;

        if (speed < 1)
            speed = 1;
        nx = mx + dx * speed;
        ny = my + dy * speed;

        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > SCREEN_W - 1) nx = SCREEN_W - 1;
        if (ny > SCREEN_H - 1) ny = SCREEN_H - 1;

        if (nx != mx || ny != my)
        {
            mx = nx;
            my = ny;
            overlay_restore();
            overlay_draw();
        }
    }

    /*
     * A press is the edge, not the level: the mask is diffed against the
     * previous field so a button held down across several fields asks
     * once.  Left answers 1, right 2; if both go down in the same field
     * the left one wins, because it is the one the scene acts on.
     */
    {
        int pressed = btn & ~btn_prev;

        btn_prev = btn;
        if (pressed & NB_BTN_L)
            clicked = 1;
        else if (pressed & NB_BTN_R)
            clicked = 2;
    }

    return clicked;
}

/* ------------------------------------------------------------------ *
 * Serial read-out -- development only, never shown on screen
 * ------------------------------------------------------------------ */

static void s_put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void s_u(unsigned v)
{
    char buf[12];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    s_put(&buf[i]);
}

static void s_i(int v)
{
    if (v < 0)
    {
        s_put("-");
        s_u((unsigned)(-v));
    }
    else
        s_u((unsigned)v);
}

void nb_pointer_dump(void)
{
    pick_colours();

    s_put(">ptr pos=");
    s_i(mx);
    s_put(",");
    s_i(my);
    s_put(" rect=");
    s_i(last_x);
    s_put(",");
    s_i(last_y);
    s_put(" ");
    s_i(last_w);
    s_put("x");
    s_i(last_h);
    s_put(" fg=");
    s_i(fg_idx);
    s_put(" bg=");
    s_i(bg_idx);
    s_put(" drawn=");
    s_i(have_ov);
    s_put(" vis=");
    s_i(nb_prefs.visible);
    s_put("\r\n");
}
