#ifndef NB_LOGO_H
#define NB_LOGO_H

#include "../../../boot/rom/gfx.h"

/*
 * The NeoBench mark, redrawn with the gfx primitives.
 *
 * The source artwork is a square off-white field carrying a teal disc on
 * the left, a diagonally split block and a small square on the right, a
 * full width off-white band with the NEOBENCH wordmark through the
 * middle, and a second cluster of triangles below it.  Everything here
 * is expressed in that 1024-unit design space and scaled into the
 * destination square, so the same code paints the 256 px wallpaper and
 * the 24 px start-orb badge without a single byte of bitmap in the ROM.
 */

#define LOGO_BG    NB_RGB(30, 61, 30)     /* #F4F4F2 field   */
#define LOGO_TEAL  NB_RGB(6, 41, 22)      /* #34A7B4         */
#define LOGO_NAVY  NB_RGB(2, 15, 10)      /* #103F55         */

/* Geometry only: disc, triangles and square, no band, no wordmark.
 * Safe to draw inside a circular badge. */
void logo_mark(int x, int y, int size);

/* Off-white band across the middle plus the scaled NEOBENCH wordmark. */
void logo_word(int x, int y, int size);

/* mark + wordmark, i.e. the whole picture */
void logo_draw(int x, int y, int size);

#endif /* NB_LOGO_H */
