/*
 * bootart.c
 * Big animated 3D ASCII art for the NeoBench loader.
 *
 *   - An extruded, beveled 3D "NEOBENCH" wordmark (bright front face
 *     over a dim offset shadow giving pseudo-3D pressed/deep depth).
 *   - A genuinely 3D rotating wireframe cube: integer fixed-point math
 *     with a hardcoded 64-entry sin table (derived cos by offset), faces
 *     depth-sorted (painter) with per-face glyph shading.
 *   - A shaded underline and a footer/tagline.
 *
 * All output goes through neo_puts() (serial console). The cube is
 * rendered one complete frame per pass, redrawn from the ANSI home
 * position.
 */

#include <stdint.h>
#include "console.h"
#include "bootart.h"

/* ------------------------------------------------------------------ */
/* 3D extruded wordmark                                                */
/* ------------------------------------------------------------------ */

#define GLYPH_ROWS 7
#define GLYPH_COLS 7

static const char *const G_N[GLYPH_ROWS] = {
    "N     N", "NN    N", "N N   N", "N  N  N",
    "N   N N", "N    NN", "N     N",
};
static const char *const G_E[GLYPH_ROWS] = {
    "NNNNNNN", "N      ", "N      ", "NNNNNNN",
    "N      ", "N      ", "NNNNNNN",
};
static const char *const G_O[GLYPH_ROWS] = {
    " NNNNN ", "N     N", "N     N", "N     N",
    "N     N", "N     N", " NNNNN ",
};
static const char *const G_B[GLYPH_ROWS] = {
    "NNNNNN ", "N     N", "N     N", "NNNNNN ",
    "N     N", "N     N", "NNNNNN ",
};
static const char *const G_C[GLYPH_ROWS] = {
    " NNNNNN", "N      ", "N      ", "N      ",
    "N      ", "N      ", " NNNNNN",
};
static const char *const G_H[GLYPH_ROWS] = {
    "N     N", "N     N", "N     N", "NNNNNNN",
    "N     N", "N     N", "N     N",
};

static const char *const *const WORD[] = {
    G_N, G_E, G_O, G_B, G_E, G_N, G_C, G_H,
};
#define WORD_LEN (int)(sizeof(WORD) / sizeof(WORD[0]))

static void emit_row(const char *r, int shadow)
{
    int i;
    if (shadow)
        neo_putc(' ');                 /* push shadow one col right */
    for (i = 0; i < GLYPH_COLS; i++)
        neo_putc(r[i] == ' ' ? ' ' : (shadow ? '.' : '#'));
}

void bootart_draw_3d_wordmark(void)
{
    int g, r;
    neo_puts("\033[1m\033[38;5;45m");
    for (r = 0; r < GLYPH_ROWS; r++)
    {
        for (g = 0; g < WORD_LEN; g++)
            emit_row(WORD[g][r], 0);           /* bright face */
        neo_putc('\n');
        for (g = 0; g < WORD_LEN; g++)
            emit_row(WORD[g][r], 1);           /* dim shadow, offset right */
        neo_putc('\n');
    }
    neo_puts("\033[0m");
}

/* ------------------------------------------------------------------ */
/* Fixed-point trig (hardcoded 64-step sin table)                      */
/* ------------------------------------------------------------------ */

#define FP 1024                       /* 10 fractional bits */

static const int32_t ST_SIN[64] = {
      0,  100,  200,  297,  392,  483,  569,  650,
    724,  792,  851,  903,  946,  980, 1004, 1019,
   1024, 1019, 1004,  980,  946,  903,  851,  792,
    724,  650,  569,  483,  392,  297,  200,  100,
      0, -100, -200, -297, -392, -483, -569, -650,
   -724, -792, -851, -903, -946, -980,-1004,-1019,
  -1024,-1019,-1004, -980, -946, -903, -851, -792,
   -724, -650, -569, -483, -392, -297, -200, -100,
};
#define ST_SINLEN 64

static int32_t s_sin(int i)
{
    return ST_SIN[((i % ST_SINLEN) + ST_SINLEN) % ST_SINLEN];
}
static int32_t s_cos(int i)
{
    return s_sin(i + (ST_SINLEN / 4));
}

/* ------------------------------------------------------------------ */
/* Rotating wireframe cube                                             */
/* ------------------------------------------------------------------ */

#define CW 40
#define CH 14

static char   cbuf[CH][CW];
static int32_t zbuf[CH][CW];

#define CUBE_N 8
static const int verts[CUBE_N][3] = {
    { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 },
    { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 },
};
static const int faceLoop[6][4] = {
    {0,1,2,3}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {0,3,7,4}, {1,2,6,5},
};
static const char faceGlyph[6] = {
    '#', '#', '*', '*', '+', '+',
};

static void plot(int x, int y, char c, int32_t z)
{
    if (x < 0 || x >= CW || y < 0 || y >= CH)
        return;
    if (z >= zbuf[y][x])
    {
        zbuf[y][x] = z;
        cbuf[y][x] = c;
    }
}

static void line(int x0, int y0, int x1, int y1, char c, int32_t z)
{
    int dx = (x1 > x0 ? x1 - x0 : x0 - x1);
    int dy = (y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int guard = 0;

    plot(x0, y0, c, z);
    if (x0 == x1 && y0 == y1)
        return;

    for (;;)
    {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
        plot(x0, y0, c, z);
        if (++guard > (CW + CH) * 2)
            return;                     /* safety against stuck edges */
        if (x0 == x1 && y0 == y1)
            return;
    }
}

static void cube_frame(int step)
{
    int px[CUBE_N], py[CUBE_N], pz[CUBE_N];
    int i, yy, xx;
    int a = step & 63;
    int b = (step >> 1) & 63;
    int32_t sa = s_sin(a), ca = s_cos(a);
    int32_t sb = s_sin(b), cb = s_cos(b);
    int scale = 9;
    int offx = CW / 2;
    int offy = CH / 2;

    for (i = 0; i < CUBE_N; i++)
    {
        /* Scale raw {-1,0,1} vertices to FP units so all rotation math
           stays in 32-bit (no 64-bit libgcc helpers, freestanding). */
        int32_t x = verts[i][0] * FP;
        int32_t y = verts[i][1] * FP;
        int32_t z = verts[i][2] * FP;

        /* Rotate Y */
        int32_t x1 = (x * ca - z * sa) / FP;
        int32_t z1 = (x * sa + z * ca) / FP;
        /* Rotate X */
        int32_t x2 = x1;
        int32_t y2 = (y * cb - z1 * sb) / FP;
        int32_t z2 = (y * sb + z1 * cb) / FP;

        /* Perspective: persp = 3 / (3 + z), z in FP units. */
        int32_t persp = (3 * FP) / (3 * FP + z2);
        if (persp < FP / 2) persp = FP / 2;
        if (persp > 2 * FP) persp = 2 * FP;

        px[i] = offx + (int)(x2 * scale * persp / (FP * FP));
        py[i] = offy + (int)(y2 * scale * persp / (FP * FP));
        pz[i] = z2;                 /* larger = toward viewer */
    }

    for (yy = 0; yy < CH; yy++)
        for (xx = 0; xx < CW; xx++)
        { cbuf[yy][xx] = ' '; zbuf[yy][xx] = -1000000; }

    /* Paint faces far-to-near by average depth. */
    for (i = 0; i < 6; i++)
    {
        int v;
        int32_t avg = 0;
        for (v = 0; v < 4; v++)
            avg += pz[faceLoop[i][v]];
        avg /= 4;
        for (v = 0; v < 4; v++)
        {
            int A = faceLoop[i][v];
            int B = faceLoop[i][(v + 1) & 3];
            line(px[A], py[A], px[B], py[B], faceGlyph[i], avg);
        }
    }
}

void bootart_draw_cube(void)
{
    int frame;

    neo_puts("\033[1m\033[38;5;39m");
    for (frame = 0; frame < 36; frame++)
    {
        cube_frame(frame);
        neo_puts("\033[H");
        for (int y = 0; y < CH; y++)
        {
            for (int x = 0; x < CW; x++)
                neo_putc(cbuf[y][x]);
            neo_putc('\n');
        }
        for (volatile int d = 0; d < 300000; d++)
            ;
    }
    neo_puts("\033[0m");
}

/* ------------------------------------------------------------------ */
/* Underline + footer                                                  */
/* ------------------------------------------------------------------ */

void bootart_draw_underline(void)
{
    int i;
    neo_puts("\033[38;5;39m");
    for (i = 0; i < 72; i++)
        neo_putc('=');
    neo_puts("\033[0m\n");
}

void bootart_draw_footer(const char *line)
{
    neo_puts("\033[1m\033[38;5;226m");
    neo_puts(line);
    neo_puts("\033[0m\n");
}
