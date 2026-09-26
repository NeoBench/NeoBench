#ifndef NB_AMIGA_H
#define NB_AMIGA_H

#include <stdint.h>

/* Target: AGA (A1200/A4000) only. */
#define NB_TARGET_AGA 1

/*
 * Desktop display mode: hires 640x512, interlaced, eight bitplanes
 * (256 colours).
 *
 * One field carries 256 of the 512 rows.  Agnus fetches with a modulo of
 * one plane pitch, so a field walks row 0,2,4,..  or 1,3,5,.. depending
 * on where the vertical blank handler started it, and the two fields
 * weave back together into the full height.  The vertical blank
 * interrupt picks the starting row for the field about to be scanned.
 *
 * Plane p lives at NB_FB_BASE + p * NB_PLANE_SIZE; each plane is
 * NB_PLANE_PITCH bytes wide (640 pixels / 8 bits) and NB_SCREEN_H rows
 * tall ($A000), so all eight planes occupy $010000..$060000.
 *
 * The console keeps its 80x32 grid: cells become 8x16 and the 8x8 font
 * is stretched over the taller cell, which preserves the proportions
 * the boot log was tuned for now that the raster is twice as tall.
 */
#define NB_SCREEN_W     640UL
#define NB_SCREEN_H     512UL
#define NB_FB_BASE      0x00010000UL
#define NB_PLANE_PITCH  80UL
#define NB_PLANE_SIZE   (NB_PLANE_PITCH * NB_SCREEN_H)   /* $A000 */

#define NB_CELL_H       16UL                            /* console cell */

/* Console palette slots (see amiga_display_init). */
#define NB_COL_BLACK    0U
#define NB_COL_GREEN    1U              /* P1 phosphor green, the boot text  */
#define NB_COL_RED      2U              /* status: fail                      */
#define NB_COL_AMBER    3U              /* status: warning                   */
#define NB_COL_WHITE    4U

/*
 * Seconds since boot, bumped by the vertical blank handler.  Nothing in
 * NeoBench has a real-time clock, so the taskbar shows an uptime rather
 * than a wall time it has no way to know.
 */
extern volatile uint32_t nb_secs;

void nb_vbl_tick(void);
void amiga_serial_init(void);
void amiga_display_init(void);
void amiga_putc(char c);
void amiga_serial_putc(char c);
void amiga_display_clear(void);
void amiga_display_vsync(void);
void amiga_display_hold(int hold);
void amiga_set_color(unsigned idx, uint8_t r, uint8_t g, uint8_t b);
void amiga_set_fg(unsigned idx);
int  amiga_display_ready(void);

#endif /* NB_AMIGA_H */
