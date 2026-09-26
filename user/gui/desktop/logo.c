/*
 * logo.c - the NeoBench mark and wordmark, drawn procedurally.
 *
 * The source artwork is a square off-white field: a teal disc on the
 * left, a diagonally split block and a small square on the right, a full
 * width off-white band carrying the NEOBENCH wordmark through the
 * middle, and a second pair of triangles below it.
 *
 * Every coordinate below lives in that 1024-unit design space and is
 * scaled into the destination square, so the same code paints the 256 px
 * desktop wallpaper and the 24 px start-orb badge without a byte of
 * bitmap data in the ROM.
 */

#include "logo.h"

/* design coordinate -> destination, keeping the artwork square */
static int dmap(int v, int size)
{
    return (int)(((int32_t)v * (int32_t)size) >> 10);
}

void logo_mark(int x, int y, int size)
{
    if (size <= 0)
        return;

    /* teal disc, left of centre */
    gfx_disc(x + dmap(375, size), y + dmap(500, size),
             dmap(245, size), LOGO_TEAL);

    /* block at 475..715 x 96..412, cut corner to corner: teal over the
     * top right, navy tucked under the bottom left */
    gfx_tri(x + dmap(475, size), y + dmap(96, size),
            x + dmap(715, size), y + dmap(96, size),
            x + dmap(715, size), y + dmap(412, size), LOGO_TEAL);
    gfx_tri(x + dmap(475, size), y + dmap(96, size),
            x + dmap(715, size), y + dmap(412, size),
            x + dmap(475, size), y + dmap(412, size), LOGO_NAVY);

    /* small teal square, top right */
    gfx_fill(x + dmap(785, size), y + dmap(140, size),
             dmap(70, size), dmap(70, size), LOGO_TEAL);

    /* lower pair, both hanging from the band: navy leaning down-left,
     * teal leaning down-right */
    gfx_tri(x + dmap(375, size), y + dmap(616, size),
            x + dmap(660, size), y + dmap(616, size),
            x + dmap(375, size), y + dmap(940, size), LOGO_NAVY);
    gfx_tri(x + dmap(660, size), y + dmap(616, size),
            x + dmap(890, size), y + dmap(616, size),
            x + dmap(890, size), y + dmap(940, size), LOGO_TEAL);
}

void logo_word(int x, int y, int size)
{
    int ts, w;

    if (size <= 0)
        return;
    ts = (size * 14) >> 10;             /* 14 design units per font pixel */
    if (ts < 1)
        return;

    /* the off-white band cuts the shapes in two */
    gfx_fill(x, y + dmap(412, size), size, dmap(616 - 412, size), LOGO_BG);

    w = 64 * ts;                        /* 8 glyphs x 8 px x scale        */
    gfx_text_s(x + (size - w) / 2, y + (size - 8 * ts) / 2,
               "NEOBENCH", LOGO_NAVY, ts);
}

void logo_draw(int x, int y, int size)
{
    logo_mark(x, y, size);
    logo_word(x, y, size);
}
