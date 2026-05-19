#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * Colour types
 * ============================================================================ */

typedef struct {
    uint8_t r, g, b, a;
} NB_Color;

/* Linear interpolation between two colours (t = 0..255) */
NB_Color nb_color_lerp(NB_Color a, NB_Color b, uint8_t t);

/*
 * Pack to RGB565 (for RTG framebuffer).
 */
uint16_t nb_to_rgb565(NB_Color c);

/* ============================================================================
 * Geometry types
 * ============================================================================ */

typedef struct {
    int32_t x, y;
} NB_Point;

typedef struct {
    uint32_t w, h;
} NB_Size;

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
} NB_Rect;

/* ============================================================================
 * Graphics mode
 * ============================================================================ */

typedef enum {
    GFX_ECS = 0,
    GFX_AGA = 1,
    GFX_RTG = 2
} NB_GfxMode;

/* ============================================================================
 * Core API
 * ============================================================================ */

bool nb_gui_init(NB_GfxMode mode);
void nb_gui_shutdown(void);

void nb_gui_begin_frame(void);
void nb_gui_end_frame(void);

uint32_t nb_screen_width(void);
uint32_t nb_screen_height(void);

/* ---- Drawing primitives ---- */

void nb_fill_rect(NB_Rect r, NB_Color c);
void nb_draw_text(int32_t x, int32_t y, const char *text, NB_Color c, int font_size);
uint32_t nb_text_width(const char *text, int font_size);
uint32_t nb_text_height(int font_size);

#endif
