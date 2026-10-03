/*
 * gfx.c - software compositor for the NeoBench desktop.
 *
 * Drawing happens in an RGB565 back buffer; gfx_present() quantises the
 * finished scene with median cut, uploads a 128-colour AGA palette and
 * bitplanes the result into the hardware frame buffer.
 *
 * No runtime library: no division (hand-rolled shift/subtract udiv and
 * reciprocal multiply), no large static buffers (runtime bump arena in
 * the $100000..$110000 Chip RAM gap).
 */

#include "gfx.h"
#include "amiga.h"
#include "font8x8.h"
#include "fontxen9.h"

#define GW  640
#define GH  512

/* ------------------------------------------------------------------ *
 * Runtime bump arena
 * ------------------------------------------------------------------ */

static uint32_t nb_arena;

void gfx_init(void)
{
    nb_arena = NB_ARENA_BASE;
}

static void *arena_get(uint32_t bytes)
{
    uint32_t p = nb_arena;

    bytes = (bytes + 1u) & ~1u;
    if (p + bytes > NB_ARENA_END)
        return 0;
    nb_arena = p + bytes;
    return (void *)(uintptr_t)p;
}

static void arena_set(uint32_t p)
{
    nb_arena = p;
}

/* ------------------------------------------------------------------ *
 * Small numeric helpers
 * ------------------------------------------------------------------ */

/* Shift/subtract 32-bit division; the freestanding link has no
 * __udivsi3, so division must be explicit and rare. */
static uint32_t udiv32(uint32_t n, uint32_t d)
{
    uint32_t q = 0, s = d;
    int sh = 0;

    if (d == 0)
        return 0;
    while (s <= n && sh < 31) {
        s <<= 1;
        sh++;
    }
    for (;;) {
        q <<= 1;
        if (n >= s) {
            n -= s;
            q |= 1u;
        }
        if (sh == 0)
            break;
        s >>= 1;
        sh--;
    }
    return q;
}

/* round(t / 255) for t <= 63*255 */
#define DIV255(t)  ((((uint32_t)(t)) * 257UL + 0x8000UL) >> 16)

#define BB  ((volatile uint16_t *)(uintptr_t)NB_BACKBUF_BASE)

/* ------------------------------------------------------------------ *
 * Geometry helpers
 * ------------------------------------------------------------------ */

/* Rounded-rectangle corner test: px,py inside the rect is kept only if
 * it lies on or inside the corner circle of radius r. */
static int corner_ok(int px, int py, int x, int y, int w, int h, int r)
{
    int dx, dy;

    if (r <= 0)
        return 1;
    if (px >= x + r && px <= x + w - 1 - r)
        return 1;
    if (py >= y + r && py <= y + h - 1 - r)
        return 1;
    dx = (px < x + r) ? (px - (x + r)) : (px - (x + w - 1 - r));
    dy = (py < y + r) ? (py - (y + r)) : (py - (y + h - 1 - r));
    return dx * dx + dy * dy <= r * r;
}

/* ------------------------------------------------------------------ *
 * Row band
 *
 * A repaint that changes one window still used to redraw the whole
 * raster: every glow, the gradient, the taskbar's blur, and then every
 * row of the pack.  The scene above the change is already sitting in
 * the back buffer and is identical row for row, so nothing outside the
 * changed rows has to be touched at all.
 *
 * gfx_band(y0, y1) narrows drawing *and* packing to those rows; every
 * primitive respects it, which is what makes the result pixel for pixel
 * the same as a full pass would have produced.  gfx_band_all() puts the
 * whole raster back -- the state a fresh boot starts in, since the flag
 * comes out of cleared .bss.
 * ------------------------------------------------------------------ */
static int band_on, band_y0, band_y1;

void gfx_band(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > GH) y1 = GH;
    if (y1 < y0) y1 = y0;
    band_y0 = y0;
    band_y1 = y1;
    band_on = 1;
}

void gfx_band_all(void)
{
    band_on = 0;
    band_y0 = 0;
    band_y1 = 0;
}

static int band_ok(int y)
{
    return !band_on || (y >= band_y0 && y < band_y1);
}

/* Clamp a rect against the screen; returns 0 when empty. */
static int clip(int *x, int *y, int *w, int *h)
{
    if (*w <= 0 || *h <= 0)
        return 0;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > GW) *w = GW - *x;
    if (*y + *h > GH) *h = GH - *y;
    if (band_on) {                      /* and then against the band   */
        int y1 = *y + *h;

        if (*y < band_y0) *y = band_y0;
        if (y1 > band_y1) y1 = band_y1;
        *h = y1 - *y;
    }
    return (*w > 0 && *h > 0);
}

/* ------------------------------------------------------------------ *
 * Primitives
 * ------------------------------------------------------------------ */

void gfx_pixel(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= GW || y >= GH)
        return;
    if (!band_ok(y))
        return;
    BB[(unsigned)y * GW + (unsigned)x] = c;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));   /* standard: dy <= 0 */
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        int e2;
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
    int i, j;

    if (!clip(&x, &y, &w, &h))
        return;
    for (j = 0; j < h; j++) {
        volatile uint16_t *p = BB + (unsigned)(y + j) * GW + (unsigned)x;
        for (i = 0; i < w; i++)
            p[i] = c;
    }
}

void gfx_fill_r(int x, int y, int w, int h, int r, uint16_t c)
{
    int i, j;

    if (!clip(&x, &y, &w, &h))
        return;
    for (j = 0; j < h; j++) {
        volatile uint16_t *p = BB + (unsigned)(y + j) * GW + (unsigned)x;
        for (i = 0; i < w; i++) {
            if (corner_ok(x + i, y + j, x, y, w, h, r))
                p[i] = c;
        }
    }
}

void gfx_vgrad(int x, int y, int w, int h, uint16_t c0, uint16_t c1)
{
    int j, y0 = y, hh = h;
    int r0 = (c0 >> 11) & 31, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
    int r1 = (c1 >> 11) & 31, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
    int dr = r1 - r0, dg = g1 - g0, db = b1 - b0;

    if (!clip(&x, &y, &w, &h))
        return;
    if (hh < 2) {
        gfx_fill(x, y, w, h, c0);
        return;
    }
    for (j = 0; j < h; j++) {
        int row = (y + j) - y0;          /* row within the original rect */
        int rr = r0, gg = g0, bb = b0;
        uint16_t c;

        if (dr >= 0) rr += (int)udiv32((uint32_t)(row * dr), (uint32_t)(hh - 1));
        else         rr -= (int)udiv32((uint32_t)(row * -dr), (uint32_t)(hh - 1));
        if (dg >= 0) gg += (int)udiv32((uint32_t)(row * dg), (uint32_t)(hh - 1));
        else         gg -= (int)udiv32((uint32_t)(row * -dg), (uint32_t)(hh - 1));
        if (db >= 0) bb += (int)udiv32((uint32_t)(row * db), (uint32_t)(hh - 1));
        else         bb -= (int)udiv32((uint32_t)(row * -db), (uint32_t)(hh - 1));

        c = (uint16_t)(((rr & 31) << 11) | ((gg & 63) << 5) | (bb & 31));
        gfx_fill(x, y + j, w, 1, c);
    }
}

/*
 * The same ramp, cut to a rounded rectangle: the tile every icon in the
 * set is drawn on.  gfx_vgrad answers per row and lets the row decide
 * its own colour; this lets each pixel of that row also decide whether
 * the corner curve takes it, which is exactly what gfx_fill_r does to a
 * single colour.  Both halves in one pass is what stops the ramp
 * showing square corners round the tile -- drawing the ramp and then
 * clipping it afterwards cannot be done with the primitives there are.
 */
void gfx_vgrad_r(int x, int y, int w, int h, int r,
                 uint16_t c0, uint16_t c1)
{
    int j, y0 = y, hh = h;
    int r0 = (c0 >> 11) & 31, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
    int r1 = (c1 >> 11) & 31, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
    int dr = r1 - r0, dg = g1 - g0, db = b1 - b0;

    if (!clip(&x, &y, &w, &h))
        return;
    if (hh < 2) {
        gfx_fill_r(x, y, w, h, r, c0);
        return;
    }
    for (j = 0; j < h; j++) {
        int row = (y + j) - y0;          /* row within the original rect */
        int rr = r0, gg = g0, bb = b0, i;
        uint16_t c;
        volatile uint16_t *p = BB + (unsigned)(y + j) * GW + (unsigned)x;

        if (dr >= 0) rr += (int)udiv32((uint32_t)(row * dr), (uint32_t)(hh - 1));
        else         rr -= (int)udiv32((uint32_t)(row * -dr), (uint32_t)(hh - 1));
        if (dg >= 0) gg += (int)udiv32((uint32_t)(row * dg), (uint32_t)(hh - 1));
        else         gg -= (int)udiv32((uint32_t)(row * -dg), (uint32_t)(hh - 1));
        if (db >= 0) bb += (int)udiv32((uint32_t)(row * db), (uint32_t)(hh - 1));
        else         bb -= (int)udiv32((uint32_t)(row * -db), (uint32_t)(hh - 1));

        c = (uint16_t)(((rr & 31) << 11) | ((gg & 63) << 5) | (bb & 31));
        for (i = 0; i < w; i++) {
            if (corner_ok(x + i, y + j, x, y, w, h, r))
                p[i] = c;
        }
    }
}

void gfx_alpha(int x, int y, int w, int h, uint16_t c, uint8_t a)
{
    int i, j;
    unsigned ia = 255u - a;
    unsigned sr = (c >> 11) & 31, sg = (c >> 5) & 63, sb = c & 31;

    if (!clip(&x, &y, &w, &h))
        return;
    for (j = 0; j < h; j++) {
        volatile uint16_t *p = BB + (unsigned)(y + j) * GW + (unsigned)x;
        for (i = 0; i < w; i++) {
            unsigned d = p[i];
            unsigned rr = DIV255(sr * a + ((d >> 11) & 31) * ia);
            unsigned gg = DIV255(sg * a + ((d >> 5) & 63) * ia);
            unsigned bb = DIV255(sb * a + (d & 31) * ia);
            p[i] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}

void gfx_alpha_r(int x, int y, int w, int h, int r, uint16_t c, uint8_t a)
{
    int i, j;
    unsigned ia = 255u - a;
    unsigned sr = (c >> 11) & 31, sg = (c >> 5) & 63, sb = c & 31;

    if (!clip(&x, &y, &w, &h))
        return;
    for (j = 0; j < h; j++) {
        volatile uint16_t *p = BB + (unsigned)(y + j) * GW + (unsigned)x;
        for (i = 0; i < w; i++) {
            unsigned d, rr, gg, bb;
            if (!corner_ok(x + i, y + j, x, y, w, h, r))
                continue;
            d = p[i];
            rr = DIV255(sr * a + ((d >> 11) & 31) * ia);
            gg = DIV255(sg * a + ((d >> 5) & 63) * ia);
            bb = DIV255(sb * a + (d & 31) * ia);
            p[i] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}

static void disc_common(int cx, int cy, int r, uint16_t c, uint8_t a, int do_alpha)
{
    int dx, dy;
    int r2 = r * r;
    unsigned ia = 255u - a;
    unsigned sr = (c >> 11) & 31, sg = (c >> 5) & 63, sb = c & 31;

    for (dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= GH || !band_ok(y))
            continue;
        for (dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            unsigned d, rr, gg, bb;
            if (x < 0 || x >= GW)
                continue;
            if (dx * dx + dy * dy > r2)
                continue;
            if (!do_alpha) {
                BB[(unsigned)y * GW + (unsigned)x] = c;
                continue;
            }
            d = BB[(unsigned)y * GW + (unsigned)x];
            rr = DIV255(sr * a + ((d >> 11) & 31) * ia);
            gg = DIV255(sg * a + ((d >> 5) & 63) * ia);
            bb = DIV255(sb * a + (d & 31) * ia);
            BB[(unsigned)y * GW + (unsigned)x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}

void gfx_disc(int cx, int cy, int r, uint16_t c)
{
    disc_common(cx, cy, r, c, 0, 0);
}

void gfx_disc_a(int cx, int cy, int r, uint16_t c, uint8_t a)
{
    disc_common(cx, cy, r, c, a, 1);
}

/* ------------------------------------------------------------------ *
 * Filled triangle
 *
 * Three vertices sorted by y, one long edge walked top to bottom and a
 * short edge either side of the middle vertex.  All three edges are
 * initialised with a single division and then stepped by addition, so
 * the scanline loop never divides.
 * ------------------------------------------------------------------ */

/* 16.16 step per scanline along an edge of dx over dy rows (dy > 0). */
static int32_t edge_step(int32_t dx, int32_t dy)
{
    uint32_t mag;
    int neg = 0;

    if (dx < 0) {
        neg = 1;
        dx = -dx;
    }
    mag = udiv32((uint32_t)dx << 16, (uint32_t)dy);
    if (mag > 0x7FFFFFFFu)
        mag = 0x7FFFFFFFu;
    return neg ? -(int32_t)mag : (int32_t)mag;
}

static void tri_span(int32_t a, int32_t b, int y, uint16_t c)
{
    int l, r, i;
    volatile uint16_t *p;

    if (y < 0 || y >= GH || !band_ok(y))
        return;
    l = (int)((a + 0x8000) >> 16);          /* round to nearest */
    r = (int)((b + 0x8000) >> 16);
    if (l > r) {
        int t = l;
        l = r;
        r = t;
    }
    if (r < 0 || l >= GW || r < l)
        return;
    if (l < 0) l = 0;
    if (r > GW - 1) r = GW - 1;

    p = BB + (unsigned)y * GW + (unsigned)l;
    for (i = l; i <= r; i++)
        p[i - l] = c;
}

void gfx_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int32_t lx, s1, s2, lstep, d1, d2;
    int y, t;

    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    if (y2 == y0)
        return;                             /* flat, nothing to fill */

    lstep = edge_step(x2 - x0, y2 - y0);
    lx    = (int32_t)((uint32_t)x0 << 16);

    if (y1 > y0) {                          /* upper half: v0 -> v1 */
        d1 = edge_step(x1 - x0, y1 - y0);
        s1 = (int32_t)((uint32_t)x0 << 16);
        for (y = y0; y < y1; y++) {
            tri_span(lx, s1, y, c);
            lx += lstep;
            s1 += d1;
        }
    }
    if (y2 > y1) {                          /* lower half: v1 -> v2 */
        d2 = edge_step(x2 - x1, y2 - y1);
        s2 = (int32_t)((uint32_t)x1 << 16);
        for (y = y1; y < y2; y++) {
            tri_span(lx, s2, y, c);
            lx += lstep;
            s2 += d2;
        }
    }
}

/* ------------------------------------------------------------------ *
 * 7x7 box blur, in place
 *
 * A box blur is separable: the forty-nine samples the window asks for
 * are two runs of seven -- a horizontal sum down each row, then a
 * vertical sum of those across each pixel.  Both runs are kept as a
 * running sum, so a step costs the sample entering the window minus
 * the one leaving it (clamped to the region, as the old per-sample
 * test did) rather than seven reads and seven tests.  The sums are
 * plain integers, so what comes out is the same picture the direct
 * window painted, only quicker.
 *
 * The region (plus a 3 px margin) is held in vertical stripes, so
 * arbitrarily tall panels fit in the 64 KiB arena.
 * ------------------------------------------------------------------ */

void gfx_blur(int x, int y, int w, int h)
{
    const int m = 3;                    /* 7x7 window */
    int sy, ey, ex, sh, ox, sx;
    uint32_t save;

    if (!clip(&x, &y, &w, &h))
        return;

    sx = x - m; if (sx < 0) sx = 0;
    sy = y - m; if (sy < 0) sy = 0;
    ex = x + w + m; if (ex > GW) ex = GW;
    ey = y + h + m; if (ey > GH) ey = GH;
    sh = ey - sy;

    save = nb_arena;
    for (ox = x; ox < x + w; ) {
        uint32_t avail = NB_ARENA_END - nb_arena;
        uint32_t maxw = avail / (uint32_t)(sh * 6u);
        int cs, ce, cw, n, j, px, py;
        uint16_t *hs;

        if (maxw > (uint32_t)(2 * m))
            maxw -= (uint32_t)(2 * m);
        else
            break;                      /* never reached for our sizes */

        cw = x + w - ox;
        if ((uint32_t)cw > maxw)
            cw = (int)maxw;

        cs = ox - m; if (cs < sx) cs = sx;
        ce = ox + cw + m; if (ce > ex) ce = ex;
        n = ce - cs;

        /* three channel sums a pixel, six bytes each */
        hs = (uint16_t *)arena_get((uint32_t)n * (uint32_t)sh * 6u);
        if (!hs)
            break;

        /*
         * Horizontal, straight out of the back buffer and into the
         * sums: one running total a row.  Each step takes the sample
         * entering the window and drops the one leaving it, clamped to
         * the stripe exactly as the per-sample test used to clamp it
         * -- seven reads become two, and the sum stays the same exact
         * integer it was.
         */
        for (j = 0; j < sh; j++) {
            const volatile uint16_t *s =
                BB + (unsigned)(sy + j) * GW + (unsigned)cs;
            uint16_t *h = hs + (uint32_t)j * (uint32_t)n * 3u;
            unsigned r = 0, g = 0, b = 0;
            int t, d;

            for (d = -m; d <= m; d++) {         /* the window at t = 0   */
                int xx = d;
                unsigned p;

                if (xx < 0) xx = 0;
                if (xx > n - 1) xx = n - 1;
                p = s[xx];
                r += (p >> 11) & 31;
                g += (p >> 5) & 63;
                b += p & 31;
            }
            h[0] = (uint16_t)r;
            h[1] = (uint16_t)g;
            h[2] = (uint16_t)b;

            for (t = 1; t < n; t++) {           /* and one step on each  */
                int in = t + m, out = t - m - 1;
                unsigned p;

                if (in > n - 1) in = n - 1;
                if (out < 0) out = 0;

                p = s[in];
                r += (p >> 11) & 31;
                g += (p >> 5) & 63;
                b += p & 31;
                p = s[out];
                r -= (p >> 11) & 31;
                g -= (p >> 5) & 63;
                b -= p & 31;

                h[t * 3 + 0] = (uint16_t)r;
                h[t * 3 + 1] = (uint16_t)g;
                h[t * 3 + 2] = (uint16_t)b;
            }
        }

        /*
         * Vertical, out of the sums and back into the picture.  The
         * seven rows a pixel draws on are the same for a whole scan
         * line, so they are worked out once above the column loop and
         * only the column moves.
         */
        for (py = y; py < y + h; py++) {
            volatile uint16_t *op = BB + (unsigned)py * GW + (unsigned)ox;
            int yy = py - sy;
            const uint16_t *rb[7];
            int d;

            for (d = 0; d <= 2 * m; d++) {
                int rr = yy - m + d;

                if (rr < 0) rr = 0;
                if (rr > sh - 1) rr = sh - 1;
                rb[d] = hs + (uint32_t)rr * (uint32_t)n * 3u;
            }

            for (px = 0; px < cw; px++) {
                const uint32_t col = (uint32_t)(ox + px - cs) * 3u;
                unsigned sr = 0, sg = 0, sb = 0;

                for (d = 0; d <= 2 * m; d++) {
                    const uint16_t *q = rb[d] + col;

                    sr += q[0];
                    sg += q[1];
                    sb += q[2];
                }
                /* /49: (sum * 334) >> 14  (334/16384 ~= 1/49) */
                sr = (sr * 334u) >> 14;
                sg = (sg * 334u) >> 14;
                sb = (sb * 334u) >> 14;
                if (sr > 31) sr = 31;
                if (sg > 63) sg = 63;
                if (sb > 31) sb = 31;
                op[px] = (uint16_t)((sr << 11) | (sg << 5) | sb);
            }
        }

        arena_set(save);
        ox += cw;
    }
    arena_set(save);
}

/* ------------------------------------------------------------------ *
 * Text
 * ------------------------------------------------------------------ */

/*
 * The face in use.  Xen is NB_FONT_XEN, zero, and .bss starts zeroed,
 * so the boot console is already setting type in Xen before Config/ has
 * been read; a file can then move the selection to the console face or
 * back again.  No initializer here: .data is write-only ROM.
 */
static int cur_font;

void gfx_font(int face)
{
    cur_font = (face == NB_FONT_SYS) ? NB_FONT_SYS : NB_FONT_XEN;
}

int gfx_font_id(void)
{
    return cur_font;
}

int gfx_font_h(void)
{
    return (cur_font == NB_FONT_SYS) ? 8 : NB_XEN_H;
}

/* The nine (or eight) row bitmap of one character, with anything
 * outside the face's range falling back to the hollow block. */
static const unsigned char *glyph_rows(unsigned char ch, int *rows)
{
    if (cur_font == NB_FONT_SYS)
    {
        *rows = 8;
        return font8x8[ch];
    }
    *rows = NB_XEN_H;
    if (ch < NB_XEN_FIRST || ch > NB_XEN_LAST)
        ch = (unsigned char)(NB_XEN_FIRST + NB_XEN_BOX);
    return fontxen9[ch - NB_XEN_FIRST];
}

void gfx_text(int x, int y, const char *s, uint16_t c)
{
    int cx = x;

    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        int row, col, rows;
        const unsigned char *bits;

        if (ch == '\n') {
            cx = x;
            y += 9;
            continue;
        }
        bits = glyph_rows(ch, &rows);
        for (row = 0; row < rows; row++) {
            unsigned char b = bits[row];
            if (!b)
                continue;
            for (col = 0; col < 8; col++) {
                if (b & (0x80u >> col))
                    gfx_pixel(cx + col, y + row, c);
            }
        }
        cx += 8;
    }
}

void gfx_text_s(int x, int y, const char *s, uint16_t c, int scale)
{
    int cx = x;

    if (scale < 1)
        scale = 1;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        int row, col, rows;
        const unsigned char *bits;

        if (ch == '\n') {
            cx = x;
            y += 9 * scale;
            continue;
        }
        bits = glyph_rows(ch, &rows);
        for (row = 0; row < rows; row++) {
            unsigned char b = bits[row];
            if (!b)
                continue;
            for (col = 0; col < 8; col++)
                if (b & (0x80u >> col))
                    gfx_fill(cx + col * scale, y + row * scale,
                             scale + 1, scale + 1, c);
        }
        cx += 8 * scale;
    }
}

/*
 * The face turned a quarter turn clockwise, for the caption down the
 * start menu's left band.  Each glyph lands with its own left edge at
 * the top of the column it goes in and its top edge on the right, which
 * is the turn that leaves a string reading downwards and its baseline
 * running along the left of it -- the way vertical type on a panel's
 * edge is set.  The advance stays eight, only along y now, so a caller
 * can centre a caption with strw() and be handed the height it asked
 * for.  One call is one line: vertical type has no newline to answer.
 */
void gfx_text_v(int x, int y, const char *s, uint16_t c)
{
    int cy = y;

    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        int row, col, rows;
        const unsigned char *bits;

        if (ch == '\n')
            continue;
        bits = glyph_rows(ch, &rows);
        for (row = 0; row < rows; row++) {
            unsigned char b = bits[row];
            if (!b)
                continue;
            for (col = 0; col < 8; col++)
                if (b & (0x80u >> col))
                    gfx_pixel(x + (rows - 1 - row), cy + col, c);
        }
        cy += 8;
    }
}

/* ------------------------------------------------------------------ *
 * Present: median cut -> palette -> planar pack
 * ------------------------------------------------------------------ */

#define NSAMP   12288u
/* Stride that keeps a full-frame sample inside NSAMP: the raster grew
 * from 256 to 512 rows, and a fixed stride would only ever reach the
 * top half of the image (the palette would then ignore the taskbar). */
#define SAMP_STRIDE (((GW * GH) / (int)NSAMP) + 1)
#define NPAL    128u                    /* indices 0..127; LUT stores idx+1 */

/*
 * A box carries the extent it was born with.
 *
 * The first version of this pass recomputed the extent of *every* box on
 * *every* iteration -- 255 trips over all 12288 samples, which is where
 * nearly all of a present's time went -- but a box's extent cannot
 * change once it exists: only a split makes a new range, and a split
 * makes both halves of it at the same time.  Scanning samples is now
 * something that happens to the one box being cut.
 */
typedef struct {
    uint32_t lo, hi;
    int rmin, rmax, gmin, gmax, bmin, bmax;
} nb_box_t;

static void box_extent(nb_box_t *b, const uint16_t *samples)
{
    uint32_t k;
    int rmin = 31, rmax = 0, gmin = 63, gmax = 0, bmin = 31, bmax = 0;

    for (k = b->lo; k < b->hi; k++) {
        uint16_t s = samples[k];
        int r = (s >> 11) & 31, g = (s >> 5) & 63, b = s & 31;

        if (r < rmin) rmin = r;
        if (r > rmax) rmax = r;
        if (g < gmin) gmin = g;
        if (g > gmax) gmax = g;
        if (b < bmin) bmin = b;
        if (b > bmax) bmax = b;
    }
    b->rmin = rmin; b->rmax = rmax;
    b->gmin = gmin; b->gmax = gmax;
    b->bmin = bmin; b->bmax = bmax;
}

/* The split score: weighted volume in ~8-bit units, and the channel to
 * cut it on -- the same arithmetic the old inner loop derived from the
 * samples each time round, read out of the box instead. */
static uint32_t box_score(const nb_box_t *b, int *chp)
{
    int er, eg, eb, ch = 0;

    if (b->hi - b->lo < 2)
        return 0;
    er = b->rmax - b->rmin;
    eg = b->gmax - b->gmin;
    eb = b->bmax - b->bmin;
    if (eg * 4 >= er * 8 && eg * 4 >= eb * 8)
        ch = 1;
    else if (eb * 8 >= er * 8)
        ch = 2;
    *chp = ch;
    return (uint32_t)(er * 8 + 1) * (uint32_t)(eg * 4 + 1) *
           (uint32_t)(eb * 8 + 1);
}

/* channel key of a 5-6-5 sample: r5, g6, b5 */
static int skey(uint16_t s, int ch)
{
    if (ch == 0)
        return (s >> 11) & 31;
    if (ch == 1)
        return (s >> 5) & 63;
    return s & 31;
}

/* Hoare partition over the inclusive range a[lo..hi]; the pivot value
 * always sits inside the scan window, which bounds both inner loops. */
static void sort_rec(uint16_t *a, uint32_t lo, uint32_t hi, int ch)
{
    uint32_t i = lo, j = hi;
    int k;

    if (lo >= hi)
        return;
    k = skey(a[lo + (hi - lo) / 2], ch);
    for (;;) {
        while (skey(a[i], ch) < k)
            i++;
        while (skey(a[j], ch) > k)
            j--;
        if (i >= j)
            break;
        {
            uint16_t tmp = a[i];
            a[i] = a[j];
            a[j] = tmp;
        }
        i++;                                /* both sides must advance */
        j--;
    }
    sort_rec(a, lo, j, ch);
    sort_rec(a, j + 1, hi, ch);
}

/* quicksort samples[lo..hi) by one channel */
static void sort_range(uint16_t *a, uint32_t lo, uint32_t hi, int ch)
{
    if (hi <= lo)
        return;
    sort_rec(a, lo, hi - 1, ch);
}

/* nearest palette colour for a 5-6-5 pixel, cached in a 15-bit LUT
 * (0 = uncomputed, stored value = palette index + 1)
 *
 * pd, when asked for, is the distance the answer came out at.  A cache
 * hit reports zero: that entry was written against this same palette
 * and was accepted when it was written, so what it cost has not
 * changed -- which is what lets a present measure only its own new
 * colours. */
static uint8_t map15(uint16_t px, const uint16_t *pal, unsigned npal,
                     uint8_t *lut, unsigned *pd)
{
    unsigned key = (((unsigned)(px >> 11) & 31u) << 10) |
                   (((unsigned)(px >> 6) & 31u) << 5) |
                   ((unsigned)px & 31u);
    unsigned r = (px >> 11) & 31, g = (px >> 5) & 63, b = px & 31;
    unsigned i, best = 0, bestd = 0xFFFFFFFFu;

    if (lut && lut[key]) {
        if (pd)
            *pd = 0;
        return (uint8_t)(lut[key] - 1);
    }

    for (i = 0; i < npal; i++) {
        unsigned p = pal[i];
        int dr = (int)r - (int)((p >> 11) & 31);
        int dg = (int)g - (int)((p >> 5) & 63);
        int db = (int)b - (int)(p & 31);
        unsigned d;

        if (dr < 0) dr = -dr;
        if (dg < 0) dg = -dg;
        if (db < 0) db = -db;
        d = (unsigned)(dr * dr * 4 + dg * dg + db * db * 4);
        if (d < bestd) {
            bestd = d;
            best = i;
            if (d == 0)
                break;
        }
    }
    if (pd)
        *pd = bestd;
    if (lut)
        lut[key] = (uint8_t)(best + 1);
    return (uint8_t)best;
}

/*
 * The palette, and the map of the frame onto it, both survive between
 * presents.
 *
 * The scene is rebuilt out of the same handful of colours most of the
 * time -- opening a window adds a few blends of teal over wallpaper, not
 * a new colour family -- so a cut taken over one frame is still a good
 * cut over the next.  And the 15-bit map is worth far more warm than the
 * cut it saves: every colour it has not seen costs a 255-entry search,
 * and a scene has thousands of distinct colours in it.
 *
 * PAL_FAR is how far a colour may land from that palette before the
 * shortcut stops being true and a fresh cut is due.  The metric is the
 * search's own -- red and blue squared against green squared, so a whole
 * step of green counts for a quarter of a step of red.
 */
#define PAL_FAR  1024u

static uint16_t pal_keep16[NPAL];
static uint8_t  pal_keep8[NPAL * 3u];
static unsigned npal_keep;              /* 0 until the first cut        */
static unsigned pal_serial;             /* bumped by every cut          */
static uint8_t  lut[32768u];            /* key -> palette index + 1     */
static unsigned lut_serial;             /* the cut the map belongs to   */
static int      pack0, pack1;           /* rows the last pack rewrote   */

/* ------------------------------------------------------------------ *
 * Development read-out: how long the last present took
 *
 * nb_fields is bumped by the vertical blank handler, which keeps
 * running while gfx_present() has the CPU, so a before/after pair
 * around the body measures the repaint in whole fields (20 ms each).
 * Nothing is printed when a present fits inside one field: after the
 * band work below that is the normal answer, and the line that does
 * appear is then the interesting one.
 * ------------------------------------------------------------------ */
static void ser_num(uint32_t v)
{
    char b[12];
    int i = 12;

    do {
        b[--i] = (char)('0' + (int)(v % 10u));     /* constant divisor */
        v /= 10u;
    } while (v && i);
    for (; i < 12; i++)
        amiga_serial_putc(b[i]);
}

static void ser_s(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void present_report(uint32_t f, unsigned cut, unsigned worst)
{
    ser_s(">present f=");
    ser_num(f);
    ser_s(" cut=");
    ser_num(cut);
    ser_s(" w=");
    ser_num(worst);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}

void gfx_present(void)
{
    const volatile uint16_t *bb = BB;
    const uint32_t f0 = nb_fields;
    uint16_t *samples;
    nb_box_t *boxes;
    uint16_t *pal16 = pal_keep16;
    uint8_t *pal8 = pal_keep8;
    uint32_t n = 0, i, nbox = 1;
    unsigned npal = 0, pass, worst = 0, cut = 0;

    gfx_init();                         /* full arena for the quantiser */

    samples = (uint16_t *)arena_get(NSAMP * 2u);
    boxes   = (nb_box_t *)arena_get((uint32_t)NPAL * sizeof(nb_box_t));
    if (!samples || !boxes)
        return;

    /* stride sample of the whole image: ~12100 samples, ~24 KiB */
    for (i = 0; i < (uint32_t)(GW * GH) && n < NSAMP; i += SAMP_STRIDE)
        samples[n++] = bb[i];

    /*
     * At most three goes.  The first takes the palette on offer -- a
     * probe through the warm map, where the samples it has not seen cost
     * exactly the search the pack would have paid for anyway -- and
     * packs.  If that pack trips over a colour the sample stride
     * stepped over and the colour is a long way from the palette, cut
     * again and pack once more.  One retry is the ordinary worst case;
     * two would mean the scene really did change colour family.
     */
    for (pass = 0; pass < 3u; pass++) {
        int recut = 0;

        if (npal_keep == 0) {
            recut = 1;
        } else if (pass == 0) {
            npal = npal_keep;
            worst = 0;
            for (i = 0; i < n; i++) {
                unsigned d;

                (void)map15(samples[i], pal16, npal, lut, &d);
                if (d > worst) worst = d;
            }
            if (worst > PAL_FAR)
                recut = 1;
        } else {
            recut = 1;                  /* the pack found a stray colour */
        }

        if (recut) {
            boxes[0].lo = 0;
            boxes[0].hi = n;
            box_extent(&boxes[0], samples);

            /* median cut */
            nbox = 1;
            while (nbox < NPAL) {
                uint32_t best = nbox, bestv = 0;
                int bestch = 0;

                for (i = 0; i < nbox; i++) {
                    int ch;
                    uint32_t v = box_score(&boxes[i], &ch);

                    if (v > bestv) {
                        bestv = v;
                        best = i;
                        bestch = ch;
                    }
                }
                if (best >= nbox || bestv <= 1)
                    break;              /* nothing left to split */

                sort_range(samples, boxes[best].lo, boxes[best].hi,
                           bestch);
                {
                    uint32_t lo = boxes[best].lo, hi = boxes[best].hi;
                    uint32_t mid = lo + (hi - lo) / 2;

                    boxes[nbox].lo = mid;
                    boxes[nbox].hi = hi;
                    box_extent(&boxes[nbox], samples);
                    boxes[best].hi = mid;
                    box_extent(&boxes[best], samples);
                    nbox++;
                }
            }

            /* representative colour = median member of each box */
            npal = 0;
            for (i = 0; i < nbox && npal < NPAL; i++) {
                uint32_t lo = boxes[i].lo, hi = boxes[i].hi;
                uint16_t s;
                unsigned r, g, b;

                if (hi == lo)
                    continue;
                s = samples[lo + (hi - lo) / 2];
                r = (s >> 11) & 31;
                g = (s >> 5) & 63;
                b = s & 31;
                pal16[npal] = (uint16_t)((r << 11) | (g << 5) | b);
                pal8[npal * 3u]     = (uint8_t)((r << 3) | (r >> 2));
                pal8[npal * 3u + 1] = (uint8_t)((g << 2) | (g >> 4));
                pal8[npal * 3u + 2] = (uint8_t)((b << 3) | (b >> 2));
                npal++;
            }
            if (npal == 0) {
                pal16[0] = 0;
                npal = 1;
            }

            npal_keep = npal;
            pal_serial++;
            if (lut_serial != pal_serial) {     /* the map is now stale */
                for (i = 0; i < 32768u; i++)
                    lut[i] = 0;
                lut_serial = pal_serial;
            }
            worst = 0;
            cut = 1;
        }

        /*
         * Hold the fetchers while the frame is rewritten.  The palette
         * goes up first, so with bitplane DMA stopped the display shows
         * COLOR00 -- slot 0 of the new palette, the wallpaper colour --
         * for the whole pack instead of a half-old/half-new scrambled
         * picture.  It is only uploaded when it has actually changed:
         * the hardware already holds the last one, and a repaint that
         * keeps the palette keeps the picture continuous too.
         */
        amiga_display_hold(1);

        if (cut) {                      /* hardware palette            */
            for (i = 0; i < npal; i++)
                amiga_set_color((unsigned)i, pal8[i * 3u],
                                pal8[i * 3u + 1], pal8[i * 3u + 2]);
            for (; i < 256u; i++)
                amiga_set_color((unsigned)i, 0, 0, 0);
        }

        /* planar pack: 8 pixels -> one byte per plane
         *
         * A fresh palette re-colours every index in the frame, so rows
         * outside the band have to be repacked too or they would be
         * read through numbers that no longer mean what they did.  That
         * is what keeps the band an optimisation and never a change of
         * picture: either the palette stands, and only the band moves,
         * or it does not, and the whole raster moves with it.
         */
        {
            uint32_t y0 = (cut || !band_on) ? 0 : (uint32_t)band_y0;
            uint32_t y1 = (cut || !band_on) ? (uint32_t)GH
                                            : (uint32_t)band_y1;

            for (i = y0; i < y1; i++) {
                const volatile uint16_t *row = bb + i * (uint32_t)GW;
                uint32_t bx;

                for (bx = 0; bx < (uint32_t)(GW / 8); bx++) {
                    uint8_t idx[8];
                    unsigned p, k;
                    uint32_t px = bx * 8u;

                    for (k = 0; k < 8; k++) {
                        unsigned d;

                        idx[k] = map15(row[px + k], pal16, npal, lut, &d);
                        if (d > worst)
                            worst = d;
                    }
                    for (p = 0; p < 8; p++) {
                        unsigned byte = 0;
                        volatile uint8_t *dst;
                        for (k = 0; k < 8; k++)
                            byte = (byte << 1) | ((idx[k] >> p) & 1u);
                        dst = (volatile uint8_t *)(uintptr_t)(
                            NB_FB_BASE + (uint32_t)p * NB_PLANE_SIZE +
                            i * NB_PLANE_PITCH + bx);
                        *dst = (uint8_t)byte;
                    }
                }
            }

            pack0 = (int)y0;
            pack1 = (int)y1;
        }

        amiga_display_hold(0);

        if (worst <= PAL_FAR)
            break;
    }

    {
        uint32_t f = nb_fields - f0;

        if (f)                          /* under one field: nothing to say */
            present_report(f, cut, worst);
    }
}

/*
 * The rows the last present actually rewrote -- the band it was handed,
 * or the whole raster when a fresh palette made every row's indices
 * mean something else.  Anything that keeps its own pixels in the frame
 * over a present (the pointer overlay) reads this to know which of them
 * are still standing.
 */
void gfx_packed(int *y0, int *y1)
{
    *y0 = pack0;
    *y1 = pack1;
}
