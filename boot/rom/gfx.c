/*
 * gfx.c - software compositor for the NeoBench desktop.
 *
 * Drawing happens in an RGB565 back buffer; gfx_present() quantises the
 * finished scene with median cut, uploads a 255-colour AGA palette and
 * bitplanes the result into the hardware frame buffer.
 *
 * No runtime library: no division (hand-rolled shift/subtract udiv and
 * reciprocal multiply), no large static buffers (runtime bump arena in
 * the $100000..$110000 Chip RAM gap).
 */

#include "gfx.h"
#include "amiga.h"
#include "font8x8.h"

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

/* Clamp a rect against the screen; returns 0 when empty. */
static int clip(int *x, int *y, int *w, int *h)
{
    if (*w <= 0 || *h <= 0)
        return 0;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > GW) *w = GW - *x;
    if (*y + *h > GH) *h = GH - *y;
    return (*w > 0 && *h > 0);
}

/* ------------------------------------------------------------------ *
 * Primitives
 * ------------------------------------------------------------------ */

void gfx_pixel(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= GW || y >= GH)
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
        if (y < 0 || y >= GH)
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

    if (y < 0 || y >= GH)
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
 * The region (plus a 3 px margin) is copied into the arena in vertical
 * stripes, blurred out of the copy and written back, so arbitrarily
 * tall panels fit in the 64 KiB arena.
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
        uint32_t maxw = avail / (uint32_t)(sh * 2);
        int cs, ce, cw, i, j, px, py;
        uint16_t *buf;

        if (maxw > (uint32_t)(2 * m))
            maxw -= (uint32_t)(2 * m);
        else
            break;                      /* never reached for our sizes */

        cw = x + w - ox;
        if ((uint32_t)cw > maxw)
            cw = (int)maxw;

        cs = ox - m; if (cs < sx) cs = sx;
        ce = ox + cw + m; if (ce > ex) ce = ex;

        buf = (uint16_t *)arena_get((uint32_t)(ce - cs) * (uint32_t)sh * 2u);
        if (!buf)
            break;

        /* copy source stripe (margin included) */
        for (j = 0; j < sh; j++) {
            const volatile uint16_t *src =
                BB + (unsigned)(sy + j) * GW + (unsigned)cs;
            uint16_t *dst = buf + (uint32_t)j * (uint32_t)(ce - cs);
            for (i = 0; i < ce - cs; i++)
                dst[i] = src[i];
        }

        /* blur the stripe's share of the region */
        for (py = y; py < y + h; py++) {
            volatile uint16_t *op = BB + (unsigned)py * GW + (unsigned)ox;
            for (px = 0; px < cw; px++) {
                int x0 = ox + px;
                unsigned sr = 0, sg = 0, sb = 0;
                int ddx, ddy;

                for (ddy = -m; ddy <= m; ddy++) {
                    int yy = py + ddy;
                    const uint16_t *row;
                    if (yy < sy) yy = sy;
                    if (yy > ey - 1) yy = ey - 1;
                    row = buf + (uint32_t)(yy - sy) * (uint32_t)(ce - cs);
                    for (ddx = -m; ddx <= m; ddx++) {
                        int xx = x0 + ddx;
                        unsigned s;
                        if (xx < cs) xx = cs;
                        if (xx > ce - 1) xx = ce - 1;
                        s = row[xx - cs];
                        sr += (s >> 11) & 31;
                        sg += (s >> 5) & 63;
                        sb += s & 31;
                    }
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

void gfx_text(int x, int y, const char *s, uint16_t c)
{
    int cx = x;

    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        int row, col;

        if (ch == '\n') {
            cx = x;
            y += 9;
            continue;
        }
        for (row = 0; row < 8; row++) {
            unsigned char bits = font8x8[ch][row];
            if (!bits)
                continue;
            for (col = 0; col < 8; col++) {
                if (bits & (0x80u >> col))
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
        int row, col;

        if (ch == '\n') {
            cx = x;
            y += 9 * scale;
            continue;
        }
        for (row = 0; row < 8; row++) {
            unsigned char bits = font8x8[ch][row];
            if (!bits)
                continue;
            for (col = 0; col < 8; col++)
                if (bits & (0x80u >> col))
                    gfx_fill(cx + col * scale, y + row * scale,
                             scale + 1, scale + 1, c);
        }
        cx += 8 * scale;
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
#define NPAL    255u                    /* indices 0..254; LUT stores idx+1 */

typedef struct {
    uint32_t lo, hi;
} nb_box_t;

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
 * (0 = uncomputed, stored value = palette index + 1) */
static uint8_t map15(uint16_t px, const uint16_t *pal, unsigned npal,
                     uint8_t *lut)
{
    unsigned key = (((unsigned)(px >> 11) & 31u) << 10) |
                   (((unsigned)(px >> 6) & 31u) << 5) |
                   ((unsigned)px & 31u);
    unsigned r = (px >> 11) & 31, g = (px >> 5) & 63, b = px & 31;
    unsigned i, best = 0, bestd = 0xFFFFFFFFu;

    if (lut && lut[key])
        return (uint8_t)(lut[key] - 1);

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
    if (lut)
        lut[key] = (uint8_t)(best + 1);
    return (uint8_t)best;
}

void gfx_present(void)
{
    const volatile uint16_t *bb = BB;
    uint16_t *samples;
    nb_box_t *boxes;
    uint16_t *pal16;
    uint8_t *pal8, *lut;
    uint32_t n = 0, i, nbox = 1;
    unsigned npal;

    gfx_init();                         /* full arena for the quantiser */

    samples = (uint16_t *)arena_get(NSAMP * 2u);
    boxes   = (nb_box_t *)arena_get(256u * sizeof(nb_box_t));
    pal16   = (uint16_t *)arena_get(NPAL * 2u);
    pal8    = (uint8_t *)arena_get(NPAL * 3u);
    if (!samples || !boxes || !pal16 || !pal8)
        return;

    /* stride sample of the whole image: ~12100 samples, ~24 KiB */
    for (i = 0; i < (uint32_t)(GW * GH) && n < NSAMP; i += SAMP_STRIDE)
        samples[n++] = bb[i];

    boxes[0].lo = 0;
    boxes[0].hi = n;

    /* median cut */
    while (nbox < NPAL) {
        uint32_t best = nbox, bestv = 0;
        int bestch = 0;

        for (i = 0; i < nbox; i++) {
            uint32_t lo = boxes[i].lo, hi = boxes[i].hi, k;
            int rmin = 31, rmax = 0, gmin = 63, gmax = 0, bmin = 31, bmax = 0;
            int er, eg, eb, ch;
            uint32_t v;

            if (hi - lo < 2)
                continue;
            for (k = lo; k < hi; k++) {
                uint16_t s = samples[k];
                int r = (s >> 11) & 31, g = (s >> 5) & 63, b = s & 31;
                if (r < rmin) rmin = r;
                if (r > rmax) rmax = r;
                if (g < gmin) gmin = g;
                if (g > gmax) gmax = g;
                if (b < bmin) bmin = b;
                if (b > bmax) bmax = b;
            }
            er = rmax - rmin;
            eg = gmax - gmin;
            eb = bmax - bmin;
            /* scale to ~8-bit units for a fair "widest channel" pick */
            ch = 0;
            if (eg * 4 >= er * 8 && eg * 4 >= eb * 8)
                ch = 1;
            else if (eb * 8 >= er * 8)
                ch = 2;
            v = (uint32_t)(er * 8 + 1) * (uint32_t)(eg * 4 + 1) *
                (uint32_t)(eb * 8 + 1);
            if (v > bestv) {
                bestv = v;
                best = i;
                bestch = ch;
            }
        }
        if (best >= nbox || bestv <= 1)
            break;                      /* nothing left to split */

        sort_range(samples, boxes[best].lo, boxes[best].hi, bestch);
        {
            uint32_t lo = boxes[best].lo, hi = boxes[best].hi;
            uint32_t mid = lo + (hi - lo) / 2;

            boxes[nbox].lo = mid;
            boxes[nbox].hi = hi;
            boxes[best].hi = mid;
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
        pal8[npal * 3u]     = (uint8_t)((r << 3) | (r >> 2));   /* expand */
        pal8[npal * 3u + 1] = (uint8_t)((g << 2) | (g >> 4));
        pal8[npal * 3u + 2] = (uint8_t)((b << 3) | (b >> 2));
        npal++;
    }
    if (npal == 0) {
        pal16[0] = 0;
        npal = 1;
    }

    /* 15-bit nearest-colour cache */
    lut = (uint8_t *)arena_get(32768u);
    if (lut) {
        for (i = 0; i < 32768u; i++)
            lut[i] = 0;
    }

    /*
     * Hold the fetchers while the frame is rewritten.  The palette below
     * is uploaded first, so with bitplane DMA stopped the display shows
     * COLOR00 -- slot 0 of the new palette, the wallpaper colour -- for
     * the whole pack instead of a half-old/half-new scrambled picture.
     */
    amiga_display_hold(1);

    /* hardware palette */
    for (i = 0; i < npal; i++)
        amiga_set_color((unsigned)i, pal8[i * 3u], pal8[i * 3u + 1],
                        pal8[i * 3u + 2]);
    for (; i < 256u; i++)
        amiga_set_color((unsigned)i, 0, 0, 0);

    /* planar pack: 8 pixels -> one byte per plane */
    for (i = 0; i < (uint32_t)GH; i++) {
        const volatile uint16_t *row = bb + i * (uint32_t)GW;
        uint32_t bx;

        for (bx = 0; bx < (uint32_t)(GW / 8); bx++) {
            uint8_t idx[8];
            unsigned p, k;
            uint32_t px = bx * 8u;

            for (k = 0; k < 8; k++)
                idx[k] = map15(row[px + k], pal16, npal, lut);
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

    amiga_display_hold(0);
}
