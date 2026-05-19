/*
 * NeoBench OS — Welcome Wizard
 * kernel/welcome.cpp
 *
 * First-boot setup wizard: 4-step modal flow to configure
 * wallpaper, hostname, sound, then launch the desktop.
 */

#include "gui.h"
#include "neobench.h"

namespace neo { namespace welcome {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const uint16 WIN_W          = 500;
static const uint16 WIN_H          = 400;
static const uint16 BTN_W          = 110;
static const uint16 BTN_H          = 32;
static const uint16 BTN_MARGIN     = 16;
static const uint16 STEP_DOT_R     = 8;
static const uint16 STEP_DOT_GAP   = 40;
static const uint16 STEP_DOT_Y     = 24;
static const uint16 TITLE_Y        = 56;
static const uint16 CONTENT_Y      = 96;
static const uint16 THUMB_W        = 120;
static const uint16 THUMB_H        = 75;
static const uint16 THUMB_PAD      = 16;
static const uint16 HOSTNAME_MAX   = 31;
static const uint16 TEXT_FIELD_W   = 260;
static const uint16 TEXT_FIELD_H   = 24;
static const uint16 CURSOR_BLINK   = 30;  // frames

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint16           s_screen_w  = 0;
static uint16           s_screen_h  = 0;
static int32            s_win_x     = 0;
static int32            s_win_y     = 0;
static uint16           s_step      = 0;   // 0-3
static gui::GfxMode     s_gfx_mode;

// Result data
static gui::wallpaper::Type s_sel_wallpaper = (gui::wallpaper::Type)0;
static char             s_hostname[32];
static uint16           s_hostname_len = 0;
static bool             s_sound_on     = true;

// Input state
static int32  s_mouse_x     = 0;
static int32  s_mouse_y     = 0;
static bool   s_lmb         = false;
static bool   s_lmb_prev    = false;
static uint16 s_prev_joy    = 0;
static uint32 s_frame_ctr   = 0;
static bool   s_done        = false;
static bool   s_text_focus  = false;

// Wallpaper preview mini-buffers (small thumbnails generated on demand)
static uint8* s_thumb_bufs[6] = { nullptr };
static bool   s_thumb_gen[6]  = { false };

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
static gui::Color col_bg()        { return gui::Color{24, 28, 48, 255}; }
static gui::Color col_win_bg()    { return gui::Color{36, 40, 60, 240}; }
static gui::Color col_win_bdr()   { return gui::Color{80, 100, 160, 255}; }
static gui::Color col_title()     { return gui::Color{240, 240, 255, 255}; }
static gui::Color col_subtitle()  { return gui::Color{160, 170, 200, 255}; }
static gui::Color col_text()      { return gui::Color{200, 205, 220, 255}; }
static gui::Color col_accent()    { return gui::Color{80, 140, 255, 255}; }
static gui::Color col_btn()       { return gui::Color{50, 80, 160, 255}; }
static gui::Color col_btn_hov()   { return gui::Color{70, 110, 200, 255}; }
static gui::Color col_btn_text()  { return gui::Color{240, 240, 255, 255}; }
static gui::Color col_dot_done()  { return gui::Color{80, 180, 80, 255}; }
static gui::Color col_dot_cur()   { return gui::Color{100, 160, 255, 255}; }
static gui::Color col_dot_dim()   { return gui::Color{60, 60, 80, 255}; }
static gui::Color col_sel_bdr()   { return gui::Color{100, 200, 255, 255}; }
static gui::Color col_field_bg()  { return gui::Color{20, 24, 40, 255}; }
static gui::Color col_field_bdr() { return gui::Color{80, 100, 140, 255}; }
static gui::Color col_cursor()    { return gui::Color{200, 220, 255, 255}; }
static gui::Color col_check()     { return gui::Color{80, 220, 80, 255}; }
static gui::Color col_toggle_on() { return gui::Color{60, 180, 80, 255}; }
static gui::Color col_toggle_off(){ return gui::Color{80, 80, 100, 255}; }
static gui::Color col_white()     { return gui::Color{255,255,255,255}; }
static gui::Color col_shadow()    { return gui::Color{0,0,0,100}; }
static gui::Color col_cyan()      { return gui::Color{0,210,230,255}; }

// ---------------------------------------------------------------------------
// Hardware mouse reading (same as desktop.cpp)
// ---------------------------------------------------------------------------
static void read_mouse() {
    volatile uint16* JOY0DAT  = (volatile uint16*)0xDFF00A;
    volatile uint8*  CIAA_PRA = (volatile uint8*)0xBFE001;

    uint16 joy = *JOY0DAT;
    int16 dx = (int16)((int8)((joy & 0xFF) - (s_prev_joy & 0xFF)));
    int16 dy = (int16)((int8)(((joy >> 8) & 0xFF) - ((s_prev_joy >> 8) & 0xFF)));
    s_prev_joy = joy;

    s_mouse_x += dx;
    s_mouse_y += dy;
    if (s_mouse_x < 0) s_mouse_x = 0;
    if (s_mouse_y < 0) s_mouse_y = 0;
    if (s_mouse_x >= s_screen_w) s_mouse_x = s_screen_w - 1;
    if (s_mouse_y >= s_screen_h) s_mouse_y = s_screen_h - 1;

    s_lmb_prev = s_lmb;
    s_lmb = !(*CIAA_PRA & (1 << 6));
}

static bool lmb_clicked() { return s_lmb && !s_lmb_prev; }

// ---------------------------------------------------------------------------
// Button helper — returns true if clicked this frame
// ---------------------------------------------------------------------------
static bool draw_button(int32 x, int32 y, uint16 w, uint16 h, const char* label) {
    bool hover = (s_mouse_x >= x && s_mouse_x < x + w &&
                  s_mouse_y >= y && s_mouse_y < y + h);
    gui::Color bg = hover ? col_btn_hov() : col_btn();

    gui::Rect r = { x, y, w, h };
    gui::draw_rounded_rect(r, 4, bg);

    int tw = gui::text_width(label, FONT_NORMAL);
    int th = gui::text_height(FONT_NORMAL);
    gui::draw_text(x + (w - tw) / 2, y + (h - th) / 2, label,
                    col_btn_text(), FONT_NORMAL);

    return hover && lmb_clicked();
}

// ---------------------------------------------------------------------------
// Step indicator dots
// ---------------------------------------------------------------------------
static void draw_step_dots() {
    int32 total_w = 4 * (STEP_DOT_R * 2) + 3 * STEP_DOT_GAP;
    int32 sx = s_win_x + (WIN_W - total_w) / 2;
    int32 sy = s_win_y + STEP_DOT_Y;

    for (uint16 i = 0; i < 4; ++i) {
        int32 cx = sx + i * (STEP_DOT_R * 2 + STEP_DOT_GAP) + STEP_DOT_R;
        int32 cy = sy + STEP_DOT_R;

        gui::Color c;
        if (i < s_step)      c = col_dot_done();
        else if (i == s_step) c = col_dot_cur();
        else                   c = col_dot_dim();

        // Draw filled circle as rounded rect
        gui::Rect dot = { cx - STEP_DOT_R, cy - STEP_DOT_R,
                          STEP_DOT_R * 2, STEP_DOT_R * 2 };
        gui::draw_rounded_rect(dot, STEP_DOT_R, c);

        // Step number
        char num[2] = { (char)('1' + i), 0 };
        int nw = gui::text_width(num, FONT_SMALL);
        gui::draw_text(cx - nw / 2, cy - gui::text_height(FONT_SMALL) / 2,
                        num, col_white(), FONT_SMALL);

        // Connecting line to next dot
        if (i < 3) {
            int32 lx1 = cx + STEP_DOT_R + 2;
            int32 lx2 = lx1 + STEP_DOT_GAP - 4;
            gui::Color lc = (i < s_step) ? col_dot_done() : col_dot_dim();
            gui::fill_rect(gui::Rect{lx1, cy - 1, lx2 - lx1, 2}, lc);
        }
    }
}

// ---------------------------------------------------------------------------
// draw_title — centered title text in window
// ---------------------------------------------------------------------------
static void draw_title(const char* text) {
    int tw = gui::text_width(text, FONT_PROP);
    gui::draw_text(s_win_x + (WIN_W - tw) / 2, s_win_y + TITLE_Y,
                    text, col_title(), FONT_PROP);
}

// ---------------------------------------------------------------------------
// Step 1 — Welcome
// ---------------------------------------------------------------------------
static void draw_step_welcome() {
    draw_title("Welcome to NeoBench");

    int32 cx = s_win_x + WIN_W / 2;
    int32 y  = s_win_y + CONTENT_Y + 10;

    // Small ASCII logo
    const char* mini_logo = "[ NeoBench OS ]";
    int lw = gui::text_width(mini_logo, FONT_PROP);
    gui::draw_text(cx - lw / 2, y, mini_logo, col_cyan(), FONT_PROP);
    y += 36;

    // System info
    char cpu_info[64];
    neo::cpu::CpuInfo cinfo;
    neo::cpu::detect(&cinfo);
    const char* cpu_model = (cinfo.type == neo::cpu::CPU_68060) ? "60" :
                            (cinfo.type == neo::cpu::CPU_68040) ? "40" : "30";
    ksprintf(cpu_info, "CPU: Motorola 680%s0", cpu_model);
    gui::draw_text(s_win_x + 40, y, cpu_info, col_text(), FONT_NORMAL);
    y += 22;

    char mem_info[64];
    uint32 chip_kb = neo::mem::get_free_chip() / 1024;
    uint32 fast_kb = neo::mem::get_free_fast() / 1024;
    ksprintf(mem_info, "RAM: %lu KB Chip, %lu KB Fast", chip_kb, fast_kb);
    gui::draw_text(s_win_x + 40, y, mem_info, col_text(), FONT_NORMAL);
    y += 22;

    const char* chipset = "Unknown";
    if (s_gfx_mode == gui::GFX_AGA) chipset = "AGA (Advanced)";
    else if (s_gfx_mode == gui::GFX_ECS) chipset = "ECS (Enhanced)";
    else if (s_gfx_mode == gui::GFX_RTG) chipset = "RTG (Retargetable)";
    char cs_info[64];
    ksprintf(cs_info, "Chipset: %s", chipset);
    gui::draw_text(s_win_x + 40, y, cs_info, col_text(), FONT_NORMAL);
    y += 36;

    const char* sub = "Let's set up your system";
    int sw = gui::text_width(sub, FONT_NORMAL);
    gui::draw_text(cx - sw / 2, y, sub, col_subtitle(), FONT_NORMAL);

    // Next button
    int32 bx = s_win_x + WIN_W - BTN_W - BTN_MARGIN;
    int32 by = s_win_y + WIN_H - BTN_H - BTN_MARGIN;
    if (draw_button(bx, by, BTN_W, BTN_H, "Next \x1a")) {
        s_step = 1;
    }
}

// ---------------------------------------------------------------------------
// Wallpaper thumbnail generation
// ---------------------------------------------------------------------------
static void ensure_thumb(uint16 idx) {
    if (idx >= 6) return;
    if (s_thumb_gen[idx]) return;

    uint32 bpp = (s_gfx_mode == gui::GFX_RTG) ? 2u : 1u;
    uint32 sz = THUMB_W * THUMB_H * bpp;
    s_thumb_bufs[idx] = (uint8*)neo::mem::alloc(sz);
    if (s_thumb_bufs[idx]) {
        gui::wallpaper::generate((gui::wallpaper::Type)idx,
                                  s_thumb_bufs[idx], THUMB_W, THUMB_H,
                                  (s_gfx_mode == gui::GFX_RTG) ? 16u : 1u);
        s_thumb_gen[idx] = true;
    }
}

static void free_thumbs() {
    for (uint16 i = 0; i < 6; ++i) {
        if (s_thumb_bufs[i]) {
            neo::mem::free(s_thumb_bufs[i]);
            s_thumb_bufs[i] = nullptr;
        }
        s_thumb_gen[i] = false;
    }
}

// ---------------------------------------------------------------------------
// Step 2 — Choose Wallpaper
// ---------------------------------------------------------------------------
static void draw_step_wallpaper() {
    draw_title("Choose Your Wallpaper");

    int32 base_x = s_win_x + (WIN_W - (3 * THUMB_W + 2 * THUMB_PAD)) / 2;
    int32 base_y = s_win_y + CONTENT_Y + 10;

    for (uint16 t = 0; t < 6; ++t) {
        uint16 col = t % 3;
        uint16 row = t / 3;
        int32 tx = base_x + col * (THUMB_W + THUMB_PAD);
        int32 ty = base_y + row * (THUMB_H + THUMB_PAD + 18);

        ensure_thumb(t);

        // Thumbnail preview
        gui::Rect tr = { tx, ty, THUMB_W, THUMB_H };
        if (s_thumb_bufs[t]) {
            {
                uint32 tp = (s_gfx_mode == gui::GFX_RTG)
                           ? (uint32)THUMB_W * 2
                           : (uint32)THUMB_W;
                gui::blit(s_thumb_bufs[t], tp, tx, ty, THUMB_W, THUMB_H);
            }
        } else {
            gui::fill_rect(tr, gui::Color{40, 50, 70, 255});
        }

        // Selection border
        if ((gui::wallpaper::Type)t == s_sel_wallpaper) {
            gui::Rect sel = { tx - 2, ty - 2, THUMB_W + 4, THUMB_H + 4 };
            gui::draw_rect(sel, col_sel_bdr());
            gui::Rect sel2 = { tx - 3, ty - 3, THUMB_W + 6, THUMB_H + 6 };
            gui::draw_rect(sel2, col_sel_bdr());
        }

        // Name label below thumbnail
        const char* name = gui::wallpaper::get_name((gui::wallpaper::Type)t);
        int nw = gui::text_width(name, FONT_SMALL);
        gui::draw_text(tx + (THUMB_W - nw) / 2, ty + THUMB_H + 3,
                        name, col_text(), FONT_SMALL);

        // Hit test for click
        if (lmb_clicked() &&
            s_mouse_x >= tx && s_mouse_x < tx + THUMB_W &&
            s_mouse_y >= ty && s_mouse_y < ty + THUMB_H) {
            s_sel_wallpaper = (gui::wallpaper::Type)t;
        }
    }

    // Back / Next buttons
    int32 by = s_win_y + WIN_H - BTN_H - BTN_MARGIN;
    if (draw_button(s_win_x + BTN_MARGIN, by, BTN_W, BTN_H, "\x1b Back")) {
        s_step = 0;
    }
    if (draw_button(s_win_x + WIN_W - BTN_W - BTN_MARGIN, by, BTN_W, BTN_H, "Next \x1a")) {
        s_step = 2;
    }
}

// ---------------------------------------------------------------------------
// Step 3 — System Configuration
// ---------------------------------------------------------------------------
static void draw_text_field(int32 x, int32 y, const char* buf, uint16 len, bool focused) {
    gui::Rect bg = { x, y, TEXT_FIELD_W, TEXT_FIELD_H };
    gui::fill_rect(bg, col_field_bg());
    gui::draw_rect(bg, focused ? col_accent() : col_field_bdr());

    // Text content
    if (len > 0) {
        gui::draw_text(x + 6, y + 4, buf, col_white(), FONT_NORMAL);
    }

    // Blinking cursor
    if (focused && ((s_frame_ctr / CURSOR_BLINK) & 1) == 0) {
        int cw = len > 0 ? gui::text_width(buf, FONT_NORMAL) : 0;
        gui::fill_rect(gui::Rect{x + 6 + cw, y + 4, 2,
                       gui::text_height(FONT_NORMAL)}, col_cursor());
    }
}

static void handle_text_input() {
    if (!neo::keyboard::key_available()) return;
    uint8 scancode = neo::keyboard::read_scancode();
    if (scancode == 0 || (scancode & 0x80)) return;  // no key or key-up

    // Check shift
    static bool shift_held = false;
    if (scancode == 0x60 || scancode == 0x61) { shift_held = true; return; }

    uint8 key = scancode & 0x7F;

    // Backspace
    if (key == 0x41) {
        if (s_hostname_len > 0) {
            s_hostname_len--;
            s_hostname[s_hostname_len] = '\0';
        }
        return;
    }

    // Enter — just unfocus
    if (key == 0x44) {
        s_text_focus = false;
        return;
    }

    // Map scancodes to ASCII (basic QWERTY)
    static const char scancode_map_lower[] = {
        // 0x00-0x0F
        '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, '0',
        // 0x10-0x1F
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, '1', '2', '3',
        // 0x20-0x2F
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, '4', '5', '6',
        // 0x30-0x3F
        0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '.', '7', '8', '9',
    };
    static const char scancode_map_upper[] = {
        '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, '0',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, '1', '2', '3',
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, '4', '5', '6',
        0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '.', '7', '8', '9',
    };

    if (key < 0x40) {
        char ch = shift_held ? scancode_map_upper[key] : scancode_map_lower[key];
        if (ch != 0 && s_hostname_len < HOSTNAME_MAX) {
            s_hostname[s_hostname_len++] = ch;
            s_hostname[s_hostname_len]   = '\0';
        }
    }

    /* Shift release is handled by the key-up path (scancode bit 7 set)
     * at the top of this function. A second read_scancode() call was removed
     * as it silently consumed the next pending keypress. */
}

static void draw_step_configure() {
    draw_title("System Configuration");

    int32 lx = s_win_x + 40;
    int32 y  = s_win_y + CONTENT_Y + 10;

    // Hostname
    gui::draw_text(lx, y, "Hostname:", col_subtitle(), FONT_NORMAL);
    y += 24;

    int32 field_x = lx;
    int32 field_y = y;
    draw_text_field(field_x, field_y, s_hostname, s_hostname_len, s_text_focus);

    // Click to focus
    if (lmb_clicked() &&
        s_mouse_x >= field_x && s_mouse_x < field_x + TEXT_FIELD_W &&
        s_mouse_y >= field_y && s_mouse_y < field_y + TEXT_FIELD_H) {
        s_text_focus = true;
    } else if (lmb_clicked()) {
        s_text_focus = false;
    }

    if (s_text_focus) {
        handle_text_input();
    }

    y += TEXT_FIELD_H + 24;

    // Display info (read-only)
    gui::draw_text(lx, y, "Display Information:", col_subtitle(), FONT_NORMAL);
    y += 22;

    char res_buf[64];
    ksprintf(res_buf, "  Resolution: %dx%d", gui::screen_width(), gui::screen_height());
    gui::draw_text(lx, y, res_buf, col_text(), FONT_NORMAL);
    y += 20;

    const char* chipset = "Unknown";
    if (s_gfx_mode == gui::GFX_AGA)      chipset = "AGA";
    else if (s_gfx_mode == gui::GFX_ECS)  chipset = "ECS";
    else if (s_gfx_mode == gui::GFX_RTG)  chipset = "RTG";
    char cs_buf[48];
    ksprintf(cs_buf, "  Chipset: %s", chipset);
    gui::draw_text(lx, y, cs_buf, col_text(), FONT_NORMAL);
    y += 20;

    char col_buf[32];
    uint16 colors = (s_gfx_mode == gui::GFX_AGA) ? INODE_SIZE : 
                    (s_gfx_mode == gui::GFX_RTG) ? 65535 : 32;
    ksprintf(col_buf, "  Colors: %d", colors);
    gui::draw_text(lx, y, col_buf, col_text(), FONT_NORMAL);
    y += 30;

    // Sound toggle
    gui::draw_text(lx, y, "Sound:", col_subtitle(), FONT_NORMAL);
    int32 tog_x = lx + gui::text_width("Sound:", FONT_NORMAL) + 16;
    int32 tog_y = y - 2;
    uint16 tog_w = 48;
    uint16 tog_h = 22;

    gui::Color tog_bg = s_sound_on ? col_toggle_on() : col_toggle_off();
    gui::Rect tog_r = { tog_x, tog_y, tog_w, tog_h };
    gui::draw_rounded_rect(tog_r, tog_h / 2, tog_bg);

    // Knob
    int32 knob_x = s_sound_on ? (tog_x + tog_w - tog_h + 2) : (tog_x + 2);
    gui::Rect knob = { knob_x, tog_y + 2, tog_h - 4, tog_h - 4 };
    gui::draw_rounded_rect(knob, (tog_h - 4) / 2, col_white());

    const char* snd_label = s_sound_on ? "ON" : "OFF";
    gui::draw_text(tog_x + tog_w + 10, y, snd_label, col_text(), FONT_NORMAL);

    if (lmb_clicked() &&
        s_mouse_x >= tog_x && s_mouse_x < tog_x + tog_w &&
        s_mouse_y >= tog_y && s_mouse_y < tog_y + tog_h) {
        s_sound_on = !s_sound_on;
    }

    // Back / Next
    int32 by = s_win_y + WIN_H - BTN_H - BTN_MARGIN;
    if (draw_button(s_win_x + BTN_MARGIN, by, BTN_W, BTN_H, "\x1b Back")) {
        s_step = 1;
    }
    if (draw_button(s_win_x + WIN_W - BTN_W - BTN_MARGIN, by, BTN_W, BTN_H, "Next \x1a")) {
        s_step = 3;
    }
}

// ---------------------------------------------------------------------------
// Step 4 — Ready
// ---------------------------------------------------------------------------
static void draw_step_ready() {
    draw_title("You're All Set!");

    int32 cx = s_win_x + WIN_W / 2;
    int32 y  = s_win_y + CONTENT_Y + 10;

    // Big checkmark
    const char* check_text = "\xfb";  // checkmark symbol or fallback
    int cw = gui::text_width("OK", FONT_PROP);
    gui::draw_rounded_rect(gui::Rect{cx - 24, y, 48, 48}, 24, col_check());
    gui::draw_text(cx - cw / 2, y + 12, "OK", col_white(), FONT_PROP);
    y += 64;

    // Summary
    gui::draw_text(s_win_x + 40, y, "Configuration Summary:", col_subtitle(), FONT_NORMAL);
    y += 28;

    char host_line[64];
    ksprintf(host_line, "  Hostname: %s", s_hostname);
    gui::draw_text(s_win_x + 40, y, host_line, col_text(), FONT_NORMAL);
    y += 22;

    const char* wp_name = gui::wallpaper::get_name(s_sel_wallpaper);
    char wp_line[64];
    ksprintf(wp_line, "  Wallpaper: %s", wp_name);
    gui::draw_text(s_win_x + 40, y, wp_line, col_text(), FONT_NORMAL);
    y += 22;

    char snd_line[32];
    ksprintf(snd_line, "  Sound: %s", s_sound_on ? "Enabled" : "Disabled");
    gui::draw_text(s_win_x + 40, y, snd_line, col_text(), FONT_NORMAL);
    y += 22;

    char gfx_line[48];
    const char* mode = (s_gfx_mode == gui::GFX_AGA) ? "AGA" :
                       (s_gfx_mode == gui::GFX_RTG) ? "RTG" : "ECS";
    ksprintf(gfx_line, "  Graphics: %s %dx%d", mode,
             gui::screen_width(), gui::screen_height());
    gui::draw_text(s_win_x + 40, y, gfx_line, col_text(), FONT_NORMAL);
    y += 36;

    const char* msg = "Click Start to enter NeoBench Desktop";
    int mw = gui::text_width(msg, FONT_NORMAL);
    gui::draw_text(cx - mw / 2, y, msg, col_subtitle(), FONT_NORMAL);

    // Back / Start buttons
    int32 by = s_win_y + WIN_H - BTN_H - BTN_MARGIN;
    if (draw_button(s_win_x + BTN_MARGIN, by, BTN_W, BTN_H, "\x1b Back")) {
        s_step = 2;
    }
    if (draw_button(s_win_x + WIN_W - BTN_W - BTN_MARGIN - 20, by,
                    BTN_W + 20, BTN_H, "Start NeoBench \x1a")) {
        s_done = true;
    }
}

// ---------------------------------------------------------------------------
// draw_window_frame — the wizard window chrome
// ---------------------------------------------------------------------------
static void draw_window_frame() {
    // Shadow
    gui::Rect shadow = { s_win_x + 4, s_win_y + 4, WIN_W, WIN_H };
    gui::alpha_blend(shadow, col_shadow());

    // Window background
    gui::Rect win = { s_win_x, s_win_y, WIN_W, WIN_H };
    gui::draw_rounded_rect(win, 8, col_win_bg());

    // Border
    gui::draw_rect(win, col_win_bdr());
}

// ---------------------------------------------------------------------------
// draw_background — simple gradient behind wizard
// ---------------------------------------------------------------------------
static void draw_background() {
    gui::draw_gradient_rect(
        gui::Rect{0, 0, (int32)s_screen_w, (int32)s_screen_h},
        gui::Color{16, 20, 40, 255},
        gui::Color{30, 40, 70, 255}
    );

    // Subtle "NeoBench" watermark at bottom
    const char* wm = "NeoBench OS Setup";
    int ww = gui::text_width(wm, FONT_SMALL);
    gui::draw_text((s_screen_w - ww) / 2, s_screen_h - 20,
                    wm, gui::Color{50, 55, 70, 255}, FONT_SMALL);
}

// ---------------------------------------------------------------------------
// run — main wizard loop
// ---------------------------------------------------------------------------
WelcomeResult run(gui::GfxMode mode) {
    s_gfx_mode  = mode;
    s_screen_w  = gui::screen_width();
    s_screen_h  = gui::screen_height();
    s_win_x     = (s_screen_w - WIN_W) / 2;
    s_win_y     = (s_screen_h - WIN_H) / 2;
    s_step      = 0;
    s_done      = false;
    s_text_focus = false;
    s_frame_ctr = 0;

    s_mouse_x = s_screen_w / 2;
    s_mouse_y = s_screen_h / 2;
    s_lmb = s_lmb_prev = false;
    s_prev_joy = *((volatile uint16*)0xDFF00A);

    // Default hostname
    const char* def_host = "neobench";
    for (s_hostname_len = 0; def_host[s_hostname_len]; ++s_hostname_len)
        s_hostname[s_hostname_len] = def_host[s_hostname_len];
    s_hostname[s_hostname_len] = '\0';

    s_sel_wallpaper = (gui::wallpaper::Type)0;
    s_sound_on      = true;

    // Clear thumbnail state
    for (uint16 i = 0; i < 6; ++i) {
        s_thumb_bufs[i] = nullptr;
        s_thumb_gen[i]  = false;
    }

    while (!s_done) {
        read_mouse();

        gui::begin_frame();

        // Background
        draw_background();

        // Window
        draw_window_frame();

        // Step dots
        draw_step_dots();

        // Step content
        switch (s_step) {
            case 0: draw_step_welcome();   break;
            case 1: draw_step_wallpaper(); break;
            case 2: draw_step_configure(); break;
            case 3: draw_step_ready();     break;
        }

        // Cursor
        gui::cursor::draw(s_mouse_x, s_mouse_y);

        gui::end_frame();
        s_frame_ctr++;
    }

    // Clean up thumbnails
    free_thumbs();

    // Build result
    WelcomeResult result;
    result.wallpaper_type = s_sel_wallpaper;
    result.sound_on       = s_sound_on;
    for (uint16 i = 0; i <= s_hostname_len && i < 31; ++i)
        result.hostname[i] = s_hostname[i];
    result.hostname[31] = '\0';

    return result;
}

}} // namespace neo::welcome
