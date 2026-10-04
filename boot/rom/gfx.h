#ifndef NB_GFX_H
#define NB_GFX_H

#include <stdint.h>

/*
 * Software compositor for the futuristic desktop scene.
 *
 * Everything is drawn into an RGB565 back buffer in Chip RAM, then
 * quantised to 128 colours and packed into the AGA planar frame buffer
 * by gfx_present().
 *
 *   $060000..$100000  RGB565 back buffer   (640 x 512 x 2)
 *   $100000..$110000  runtime arena, 64 KiB gap before BSS; all
 *                      scratch (median-cut samples, colour LUT,
 *                      blur stripes) is allocated here at run time so
 *                      nothing large ever lands in the ROM image.
 */

#define NB_BACKBUF_BASE   0x00060000UL
#define NB_ARENA_BASE     0x00100000UL
#define NB_ARENA_END      0x00110000UL

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
void gfx_vgrad_r(int x, int y, int w, int h, int r,
                 uint16_t c0, uint16_t c1);
void gfx_alpha(int x, int y, int w, int h, uint16_t c, uint8_t a);
void gfx_alpha_r(int x, int y, int w, int h, int r, uint16_t c, uint8_t a);
void gfx_disc(int cx, int cy, int r, uint16_t c);
void gfx_disc_a(int cx, int cy, int r, uint16_t c, uint8_t a);
void gfx_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

/* In-place 7x7 box blur of a region (used to pre-blur the wallpaper
 * underneath the glass panels). Scratch comes from the arena. */
void gfx_blur(int x, int y, int w, int h);

/*
 * Text faces.  Xen is the standard -- eight pixel advance, nine rows,
 * one pixel strokes -- with the 8x8 console face kept behind it for
 * comparison and Xen11 the same face cut at eleven rows, for a desk
 * that wants its type a size bigger.  Config/screen.cfg chooses with
 * "font ="; the face selected here is the one every gfx_text() caller
 * gets, so one preference restyles the whole desktop.
 */
#define NB_FONT_XEN   0
#define NB_FONT_SYS   1
#define NB_FONT_XEN11 2

void gfx_font(int face);
int  gfx_font_id(void);
int  gfx_font_h(void);
int  gfx_font_pitch(void);

/* Bitmap text, MSB-left, eight pixels of advance either way. */
void gfx_text(int x, int y, const char *s, uint16_t c);

/* Same, but every font pixel becomes a scale x scale block and the
 * advance is 8 * scale.  Used for the scaled-up NEOBENCH wordmark. */
void gfx_text_s(int x, int y, const char *s, uint16_t c, int scale);

/* The same face turned a quarter turn clockwise, for type standing in a
 * vertical strip: the string reads down the strip, so the advance is
 * eight along y and the glyph's own rows run across x.  One call is one
 * line -- vertical type has no newline. */
void gfx_text_v(int x, int y, const char *s, uint16_t c);

/*
 * Restrict drawing *and* packing to a band of rows -- y0 inclusive, y1
 * exclusive -- so a repaint of one window does not pay for the whole
 * raster.  gfx_band_all() puts every row back; that is the state a boot
 * starts in.
 */
void gfx_band(int y0, int y1);
void gfx_band_all(void);

/* Median-cut the back buffer to 128 colours, upload the AGA palette
 * and pack the RGB565 image into the eight bitplanes. */
void gfx_present(void);

/* The rows the last gfx_present() actually rewrote: the band it was
 * handed, or 0..GH when a fresh palette forced every row's indices to
 * be repacked. */
void gfx_packed(int *y0, int *y1);

#endif /* NB_GFX_H */
