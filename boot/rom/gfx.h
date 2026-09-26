#ifndef NB_GFX_H
#define NB_GFX_H

#include <stdint.h>

/*
 * Software compositor for the Aero desktop scene.
 *
 * Everything is drawn into an RGB565 back buffer in Chip RAM, then
 * quantised to 256 colours and packed into the AGA planar frame buffer
 * by gfx_present().
 *
 *   $040000..$090000   RGB565 back buffer   (640 x 256 x 2)
 *   $090000..$0A0000   runtime arena, 64 KiB gap before BSS; all
 *                      scratch (median-cut samples, colour LUT,
 *                      blur stripes) is allocated here at run time so
 *                      nothing large ever lands in the ROM image.
 */

#define NB_BACKBUF_BASE   0x00040000UL
#define NB_ARENA_BASE     0x00090000UL
#define NB_ARENA_END      0x000A0000UL

/* 5-6-5 packed colour: r 5 bits, g 6 bits, b 5 bits. */
#define NB_RGB(r, g, b) \
    ((uint16_t)(((((uint16_t)(r)) & 31U) << 11) | \
                 (((uint16_t)(g) & 63U) << 5) | \
                 (((uint16_t)(b)) & 31U)))

void gfx_init(void);

void gfx_pixel(int x, int y, uint16_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_fill_r(int x, int y, int w, int h, int r, uint16_t c);
void gfx_vgrad(int x, int y, int w, int h, uint16_t c0, uint16_t c1);
void gfx_alpha(int x, int y, int w, int h, uint16_t c, uint8_t a);
void gfx_alpha_r(int x, int y, int w, int h, int r, uint16_t c, uint8_t a);
void gfx_disc(int cx, int cy, int r, uint16_t c);
void gfx_disc_a(int cx, int cy, int r, uint16_t c, uint8_t a);

/* In-place 7x7 box blur of a region (used to pre-blur the wallpaper
 * underneath the glass panels). Scratch comes from the arena. */
void gfx_blur(int x, int y, int w, int h);

/* 8x8 bitmap text, MSB-left, from font8x8.h. */
void gfx_text(int x, int y, const char *s, uint16_t c);

/* Median-cut the back buffer to 256 colours, upload the AGA palette
 * and pack the RGB565 image into the eight bitplanes. */
void gfx_present(void);

#endif /* NB_GFX_H */
