#ifndef NB_AMIGA_H
#define NB_AMIGA_H

#include <stdint.h>

/* Target: AGA (A1200/A4000) only. */
#define NB_TARGET_AGA 1

/*
 * Desktop display mode: hires 640x256, eight bitplanes (256 colours).
 *
 * Plane p lives at NB_FB_BASE + p * NB_PLANE_SIZE; each plane is
 * NB_PLANE_PITCH bytes wide (640 pixels / 8 bits) and NB_SCREEN_H rows tall.
 */
#define NB_SCREEN_W     640UL
#define NB_SCREEN_H     256UL
#define NB_FB_BASE      0x00010000UL
#define NB_PLANE_PITCH  80UL
#define NB_PLANE_SIZE   (NB_PLANE_PITCH * NB_SCREEN_H)   /* $5000 */

/* Console palette slots (see amiga_display_init). */
#define NB_COL_BLACK    0U
#define NB_COL_GREEN    1U              /* P1 phosphor green, the boot text  */
#define NB_COL_RED      2U              /* status: fail                      */
#define NB_COL_AMBER    3U              /* status: warning                   */
#define NB_COL_WHITE    4U

void amiga_serial_init(void);
void amiga_display_init(void);
void amiga_putc(char c);
void amiga_serial_putc(char c);
void amiga_display_clear(void);
void amiga_display_vsync(void);
void amiga_set_color(unsigned idx, uint8_t r, uint8_t g, uint8_t b);
void amiga_set_fg(unsigned idx);
int  amiga_display_ready(void);

#endif /* NB_AMIGA_H */
