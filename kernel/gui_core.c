#include "../include/gui.h"
#include "../include/neobench.h"

/* 
 * NeoBench GUI - C99 Implementation
 * Stubs to allow linking. Full implementation needed.
 */

NB_Color nb_color_lerp(NB_Color a, NB_Color b, uint8_t t) {
    NB_Color res;
    res.r = (uint8_t)((uint16_t)a.r * (255 - t) / 255 + (uint16_t)b.r * t / 255);
    res.g = (uint8_t)((uint16_t)a.g * (255 - t) / 255 + (uint16_t)b.g * t / 255);
    res.b = (uint8_t)((uint16_t)a.b * (255 - t) / 255 + (uint16_t)b.b * t / 255);
    res.a = 255;
    return res;
}

uint16_t nb_to_rgb565(NB_Color c) {
    return (uint16_t)(((uint16_t)(c.r >> 3) << 11) | ((uint16_t)(c.g >> 2) << 5) | (uint16_t)(c.b >> 3));
}

bool nb_gui_init(NB_GfxMode mode) { return true; }
void nb_gui_shutdown(void) {}
void nb_gui_begin_frame(void) {}
void nb_gui_end_frame(void) {}
uint32_t nb_screen_width(void) { return 640; }
uint32_t nb_screen_height(void) { return 480; }
void nb_fill_rect(NB_Rect r, NB_Color c) {}
void nb_draw_text(int32_t x, int32_t y, const char *text, NB_Color c, int font_size) {}
uint32_t nb_text_width(const char *text, int font_size) { return 0; }
uint32_t nb_text_height(int font_size) { return 16; }
