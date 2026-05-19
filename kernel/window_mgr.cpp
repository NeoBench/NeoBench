/*
 * NeoBench Kernel — Window Manager
 * Aero-glass style window chrome with full drag/resize support
 * Target: M68K (68030/040/060) bare-metal Amiga
 */

#include "gui.h"
#include "neobench.h"

namespace neo {
namespace wm {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const uint32 MAX_WINDOWS        = 32;
static const uint32 TITLE_BAR_HEIGHT   = 30;
static const uint32 BORDER_WIDTH       = 1;
static const uint32 SHADOW_SIZE        = 2;
static const uint32 BTN_WIDTH          = 28;
static const uint32 BTN_HEIGHT         = 20;
static const uint32 BTN_MARGIN         = 4;
static const uint32 MIN_WIN_W          = 160;
static const uint32 MIN_WIN_H          = 80;

// Window flags
static const uint32 WF_VISIBLE    = 0x0001;
static const uint32 WF_MINIMIZED  = 0x0002;
static const uint32 WF_MAXIMIZED  = 0x0004;
static const uint32 WF_MODAL      = 0x0008;
static const uint32 WF_RESIZABLE  = 0x0010;

// ---------------------------------------------------------------------------
// Hit zones
// ---------------------------------------------------------------------------

enum HitZone {
    HIT_NONE = 0,
    HIT_TITLE,
    HIT_CLOSE,
    HIT_MIN,
    HIT_MAX,
    HIT_CONTENT,
    HIT_BORDER_N,
    HIT_BORDER_S,
    HIT_BORDER_E,
    HIT_BORDER_W,
    HIT_BORDER_NE,
    HIT_BORDER_NW,
    HIT_BORDER_SE,
    HIT_BORDER_SW
};

// ---------------------------------------------------------------------------
// Window structure
// ---------------------------------------------------------------------------

struct Window {
    uint32 id;
    char   title[64];
    Rect   frame;           // outer bounds including chrome
    Rect   content;         // inner client area
    Rect   restore_frame;   // saved frame before maximize
    uint32 flags;
    uint32 app_id;
    uint8* content_buf;
    bool   dirty;
    bool   focused;
    bool   active;          // slot in use
};

// ---------------------------------------------------------------------------
// Colors — Aero glass palette
// ---------------------------------------------------------------------------

static const Color COL_TITLE_GRAD_TOP     = {  50,  80, 130, 180 };
static const Color COL_TITLE_GRAD_BOT     = {  20,  40,  70, 200 };
static const Color COL_TITLE_FOCUSED_TOP  = {  60, 100, 160, 200 };
static const Color COL_TITLE_FOCUSED_BOT  = {  25,  50,  90, 220 };
static const Color COL_TITLE_TEXT          = { 255, 255, 255, 255 };
static const Color COL_BORDER_OUTER       = {  20,  20,  30, 230 };
static const Color COL_BORDER_INNER       = { 140, 170, 210, 100 };
static const Color COL_CONTENT_BG         = { 240, 240, 240, 255 };
static const Color COL_SHADOW             = {   0,   0,   0,  80 };
static const Color COL_BTN_HOVER          = { 255, 255, 255,  60 };
static const Color COL_CLOSE_HOVER        = { 220,  50,  50, 200 };
static const Color COL_BTN_GLYPH          = { 255, 255, 255, 220 };
static const Color COL_GLASS              = {  40,  60,  80, 160 };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static Window   windows[MAX_WINDOWS];
static uint32   z_order[MAX_WINDOWS];   // indices into windows[], back-to-front
static uint32   window_count   = 0;
static uint32   next_id        = 1;

// Drag / resize state
static Window*  drag_window    = nullptr;
static int32    drag_offset_x  = 0;
static int32    drag_offset_y  = 0;
static HitZone  drag_mode      = HIT_NONE;
static int32    resize_start_x = 0;
static int32    resize_start_y = 0;
static Rect     resize_origin  = { 0, 0, 0, 0 };

// Hover state for button highlights
static Window*  hover_window   = nullptr;
static HitZone  hover_zone     = HIT_NONE;

// ---------------------------------------------------------------------------
// Helpers — simple string copy
// ---------------------------------------------------------------------------

static void str_copy(char* dst, const char* src, uint32 max_len) {
    uint32 i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32 str_len(const char* s) {
    uint32 n = 0;
    while (s[n] != '\0') n++;
    return n;
}

// ---------------------------------------------------------------------------
// Z-order helpers
// ---------------------------------------------------------------------------

static void rebuild_z_order() {
    uint32 count = 0;
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) {
            z_order[count++] = i;
        }
    }
    window_count = count;
}

static int32 z_index_of(Window* w) {
    for (uint32 i = 0; i < window_count; i++) {
        if (&windows[z_order[i]] == w) return (int32)i;
    }
    return -1;
}

static void update_content_rect(Window* w) {
    w->content.x = w->frame.x + BORDER_WIDTH;
    w->content.y = w->frame.y + TITLE_BAR_HEIGHT;
    w->content.w = w->frame.w - BORDER_WIDTH * 2;
    w->content.h = w->frame.h - TITLE_BAR_HEIGHT - BORDER_WIDTH;
}

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

void init() {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        windows[i].active = false;
        windows[i].id = 0;
        z_order[i] = 0;
    }
    window_count = 0;
    next_id = 1;
    drag_window = nullptr;
    drag_mode = HIT_NONE;
    hover_window = nullptr;
    hover_zone = HIT_NONE;
}

void shutdown() {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].content_buf) {
            neo::memory::free(windows[i].content_buf);
            windows[i].content_buf = nullptr;
        }
        windows[i].active = false;
    }
    window_count = 0;
}

// ---------------------------------------------------------------------------
// Window creation / destruction
// ---------------------------------------------------------------------------

Window* create(const char* title, int32 x, int32 y, uint32 w, uint32 h, uint32 flags) {
    // Find free slot
    int32 slot = -1;
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            slot = (int32)i;
            break;
        }
    }
    if (slot < 0) return nullptr;  // no free slots

    Window* win = &windows[slot];
    win->id = next_id++;
    str_copy(win->title, title, 64);
    win->frame.x = x;
    win->frame.y = y;
    win->frame.w = (w < MIN_WIN_W) ? MIN_WIN_W : w;
    win->frame.h = (h < MIN_WIN_H) ? MIN_WIN_H : h;
    win->restore_frame = win->frame;
    win->flags = flags | WF_VISIBLE;
    win->app_id = 0;
    win->content_buf = nullptr;
    win->dirty = true;
    win->focused = false;
    win->active = true;

    update_content_rect(win);

    // Add to top of z-order
    z_order[window_count] = (uint32)slot;
    window_count++;

    // Focus the new window
    set_focused(win);

    return win;
}

void destroy(Window* w) {
    if (!w || !w->active) return;

    if (w->content_buf) {
        neo::memory::free(w->content_buf);
        w->content_buf = nullptr;
    }

    w->active = false;

    if (drag_window == w) {
        drag_window = nullptr;
        drag_mode = HIT_NONE;
    }
    if (hover_window == w) {
        hover_window = nullptr;
        hover_zone = HIT_NONE;
    }

    rebuild_z_order();

    // Focus topmost remaining window
    if (window_count > 0) {
        set_focused(&windows[z_order[window_count - 1]]);
    }
}

// ---------------------------------------------------------------------------
// Window operations
// ---------------------------------------------------------------------------

void move(Window* w, int32 x, int32 y) {
    if (!w || !w->active) return;
    w->frame.x = x;
    w->frame.y = y;
    update_content_rect(w);
    w->dirty = true;
}

void resize(Window* w, uint32 nw, uint32 nh) {
    if (!w || !w->active) return;
    if (nw < MIN_WIN_W) nw = MIN_WIN_W;
    if (nh < MIN_WIN_H) nh = MIN_WIN_H;
    w->frame.w = nw;
    w->frame.h = nh;
    update_content_rect(w);
    w->dirty = true;
}

void minimize(Window* w) {
    if (!w || !w->active) return;
    w->flags |= WF_MINIMIZED;
    w->flags &= ~WF_VISIBLE;
    w->dirty = true;

    // Focus next visible window
    for (int32 i = (int32)window_count - 1; i >= 0; i--) {
        Window* cand = &windows[z_order[i]];
        if (cand != w && cand->active && (cand->flags & WF_VISIBLE)) {
            set_focused(cand);
            return;
        }
    }
}

void maximize(Window* w) {
    if (!w || !w->active) return;
    if (w->flags & WF_MAXIMIZED) return;

    w->restore_frame = w->frame;
    w->frame.x = 0;
    w->frame.y = 0;
    w->frame.w = gui::screen_width();
    // Leave room for taskbar (40px)
    w->frame.h = gui::screen_height() - 40;
    w->flags |= WF_MAXIMIZED;
    w->flags |= WF_VISIBLE;
    w->flags &= ~WF_MINIMIZED;
    update_content_rect(w);
    w->dirty = true;
}

void restore(Window* w) {
    if (!w || !w->active) return;

    if (w->flags & WF_MAXIMIZED) {
        w->frame = w->restore_frame;
        w->flags &= ~WF_MAXIMIZED;
    }
    w->flags |= WF_VISIBLE;
    w->flags &= ~WF_MINIMIZED;
    update_content_rect(w);
    w->dirty = true;
}

void bring_to_front(Window* w) {
    if (!w || !w->active) return;

    int32 idx = z_index_of(w);
    if (idx < 0) return;

    // Shift everything above it down
    uint32 slot_val = z_order[idx];
    for (uint32 i = (uint32)idx; i < window_count - 1; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[window_count - 1] = slot_val;
}

void set_focused(Window* w) {
    // Unfocus all
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        windows[i].focused = false;
    }
    if (w && w->active) {
        w->focused = true;
        bring_to_front(w);
        w->dirty = true;
    }
}

Window* get_focused() {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].focused) return &windows[i];
    }
    return nullptr;
}

Window* find_at(int32 x, int32 y) {
    // Search top-to-bottom in z-order
    for (int32 i = (int32)window_count - 1; i >= 0; i--) {
        Window* w = &windows[z_order[i]];
        if (!w->active) continue;
        if (!(w->flags & WF_VISIBLE)) continue;

        if (x >= w->frame.x && x < w->frame.x + (int32)w->frame.w &&
            y >= w->frame.y && y < w->frame.y + (int32)w->frame.h) {
            return w;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

static const int32 RESIZE_MARGIN = 5;

HitZone hit_test(Window* w, int32 x, int32 y) {
    if (!w || !w->active) return HIT_NONE;

    int32 fx = w->frame.x;
    int32 fy = w->frame.y;
    int32 fw = (int32)w->frame.w;
    int32 fh = (int32)w->frame.h;

    // Outside window entirely?
    if (x < fx || x >= fx + fw || y < fy || y >= fy + fh) {
        return HIT_NONE;
    }

    // Title bar buttons (right side)
    int32 bar_y = fy;
    int32 bar_bottom = fy + (int32)TITLE_BAR_HEIGHT;

    if (y >= bar_y && y < bar_bottom) {
        // Close button (rightmost)
        int32 close_x = fx + fw - BORDER_WIDTH - BTN_MARGIN - BTN_WIDTH;
        int32 btn_y = bar_y + (TITLE_BAR_HEIGHT - BTN_HEIGHT) / 2;
        if (x >= close_x && x < close_x + (int32)BTN_WIDTH &&
            y >= btn_y && y < btn_y + (int32)BTN_HEIGHT) {
            return HIT_CLOSE;
        }

        // Maximize button
        int32 max_x = close_x - BTN_MARGIN - BTN_WIDTH;
        if (x >= max_x && x < max_x + (int32)BTN_WIDTH &&
            y >= btn_y && y < btn_y + (int32)BTN_HEIGHT) {
            return HIT_MAX;
        }

        // Minimize button
        int32 min_x = max_x - BTN_MARGIN - BTN_WIDTH;
        if (x >= min_x && x < min_x + (int32)BTN_WIDTH &&
            y >= btn_y && y < btn_y + (int32)BTN_HEIGHT) {
            return HIT_MIN;
        }

        // Rest of title bar = drag area
        return HIT_TITLE;
    }

    // Resizable border hit zones
    if (w->flags & WF_RESIZABLE) {
        bool near_left   = (x < fx + RESIZE_MARGIN);
        bool near_right  = (x >= fx + fw - RESIZE_MARGIN);
        bool near_top    = (y < fy + RESIZE_MARGIN);
        bool near_bottom = (y >= fy + fh - RESIZE_MARGIN);

        if (near_top && near_left)     return HIT_BORDER_NW;
        if (near_top && near_right)    return HIT_BORDER_NE;
        if (near_bottom && near_left)  return HIT_BORDER_SW;
        if (near_bottom && near_right) return HIT_BORDER_SE;
        if (near_top)                  return HIT_BORDER_N;
        if (near_bottom)               return HIT_BORDER_S;
        if (near_left)                 return HIT_BORDER_W;
        if (near_right)                return HIT_BORDER_E;
    }

    // Content area
    return HIT_CONTENT;
}

// ---------------------------------------------------------------------------
// Drawing — Aero glass chrome
// ---------------------------------------------------------------------------

static void draw_shadow(Window* w) {
    // Right edge shadow
    Rect right_shadow = {
        w->frame.x + (int32)w->frame.w,
        w->frame.y + (int32)SHADOW_SIZE,
        SHADOW_SIZE,
        w->frame.h
    };
    gui::alpha_blend(right_shadow, COL_SHADOW);

    // Bottom edge shadow
    Rect bottom_shadow = {
        w->frame.x + (int32)SHADOW_SIZE,
        w->frame.y + (int32)w->frame.h,
        w->frame.w,
        SHADOW_SIZE
    };
    gui::alpha_blend(bottom_shadow, COL_SHADOW);
}

static void draw_title_bar(Window* w) {
    int32 fx = w->frame.x;
    int32 fy = w->frame.y;
    uint32 fw = w->frame.w;

    Rect title_rect = { fx, fy, fw, TITLE_BAR_HEIGHT };

    // Glass gradient background
    if (w->focused) {
        gui::draw_gradient_rect(title_rect, COL_TITLE_FOCUSED_TOP, COL_TITLE_FOCUSED_BOT);
    } else {
        gui::draw_gradient_rect(title_rect, COL_TITLE_GRAD_TOP, COL_TITLE_GRAD_BOT);
    }

    // Glass overlay
    gui::alpha_blend(title_rect, COL_GLASS);

    // Title text — centered
    uint32 font = 1; // FONT_NORMAL
    uint32 tw = gui::text_width(w->title, font);
    uint32 th = gui::text_height(font);
    int32 text_x = fx + ((int32)fw - (int32)tw) / 2;
    int32 text_y = fy + ((int32)TITLE_BAR_HEIGHT - (int32)th) / 2;
    gui::draw_text(text_x, text_y, w->title, COL_TITLE_TEXT, font);

    // --- Title bar buttons ---
    int32 btn_y = fy + ((int32)TITLE_BAR_HEIGHT - (int32)BTN_HEIGHT) / 2;

    // Close button (rightmost)
    int32 close_x = fx + (int32)fw - (int32)BORDER_WIDTH - (int32)BTN_MARGIN - (int32)BTN_WIDTH;
    Rect close_rect = { close_x, btn_y, BTN_WIDTH, BTN_HEIGHT };
    if (hover_window == w && hover_zone == HIT_CLOSE) {
        gui::fill_rect(close_rect, COL_CLOSE_HOVER);
    } else {
        gui::alpha_blend(close_rect, COL_BTN_HOVER);
    }
    // × glyph
    int32 gx = close_x + (int32)BTN_WIDTH / 2 - 4;
    int32 gy = btn_y + (int32)BTN_HEIGHT / 2 - 5;
    gui::draw_line(gx, gy, gx + 8, gy + 8, COL_BTN_GLYPH);
    gui::draw_line(gx + 8, gy, gx, gy + 8, COL_BTN_GLYPH);
    gui::draw_line(gx + 1, gy, gx + 9, gy + 8, COL_BTN_GLYPH);
    gui::draw_line(gx + 9, gy, gx + 1, gy + 8, COL_BTN_GLYPH);

    // Maximize button
    int32 max_x = close_x - (int32)BTN_MARGIN - (int32)BTN_WIDTH;
    Rect max_rect = { max_x, btn_y, BTN_WIDTH, BTN_HEIGHT };
    if (hover_window == w && hover_zone == HIT_MAX) {
        gui::alpha_blend(max_rect, { 255, 255, 255, 80 });
    } else {
        gui::alpha_blend(max_rect, COL_BTN_HOVER);
    }
    // □ glyph
    int32 mx = max_x + (int32)BTN_WIDTH / 2 - 4;
    int32 my = btn_y + (int32)BTN_HEIGHT / 2 - 4;
    gui::draw_rect({ mx, my, 9, 9 }, COL_BTN_GLYPH);
    gui::draw_line(mx, my + 1, mx + 8, my + 1, COL_BTN_GLYPH);

    // Minimize button
    int32 min_x = max_x - (int32)BTN_MARGIN - (int32)BTN_WIDTH;
    Rect min_rect = { min_x, btn_y, BTN_WIDTH, BTN_HEIGHT };
    if (hover_window == w && hover_zone == HIT_MIN) {
        gui::alpha_blend(min_rect, { 255, 255, 255, 80 });
    } else {
        gui::alpha_blend(min_rect, COL_BTN_HOVER);
    }
    // ─ glyph (horizontal line in center)
    int32 lx = min_x + (int32)BTN_WIDTH / 2 - 4;
    int32 ly = btn_y + (int32)BTN_HEIGHT / 2;
    gui::draw_line(lx, ly, lx + 8, ly, COL_BTN_GLYPH);
    gui::draw_line(lx, ly + 1, lx + 8, ly + 1, COL_BTN_GLYPH);
}

static void draw_borders(Window* w) {
    int32 fx = w->frame.x;
    int32 fy = w->frame.y;
    uint32 fw = w->frame.w;
    uint32 fh = w->frame.h;

    // Outer dark border
    gui::draw_rect({ fx, fy, fw, fh }, COL_BORDER_OUTER);

    // Inner highlight border
    if (fw > 2 && fh > 2) {
        gui::draw_rect({ fx + 1, fy + 1, fw - 2, fh - 2 }, COL_BORDER_INNER);
    }
}

static void draw_content_area(Window* w) {
    // Solid background
    gui::fill_rect(w->content, COL_CONTENT_BG);

    // If the window has a content buffer, blit it here
    // (App-specific rendering would go here)
    if (w->content_buf) {
        // Content buffer drawing would be handled by app subsystem
        // For now we just show the background
    }
}

void draw_window(Window* w) {
    if (!w || !w->active) return;
    if (!(w->flags & WF_VISIBLE)) return;
    if (w->flags & WF_MINIMIZED) return;

    // Set clipping to the window + shadow area
    Rect clip = {
        w->frame.x,
        w->frame.y,
        w->frame.w + SHADOW_SIZE,
        w->frame.h + SHADOW_SIZE
    };
    gui::set_clip(clip);

    // Draw shadow first (behind the window)
    draw_shadow(w);

    // Draw content area
    draw_content_area(w);

    // Draw title bar with glass effect
    draw_title_bar(w);

    // Draw window borders on top
    draw_borders(w);

    gui::clear_clip();

    w->dirty = false;
}

void draw_all() {
    // Draw back-to-front in z-order
    for (uint32 i = 0; i < window_count; i++) {
        Window* w = &windows[z_order[i]];
        if (w->active && (w->flags & WF_VISIBLE) && !(w->flags & WF_MINIMIZED)) {
            draw_window(w);
        }
    }
}

// ---------------------------------------------------------------------------
// Hover update
// ---------------------------------------------------------------------------

void update_hover(int32 mx, int32 my) {
    Window* w = find_at(mx, my);
    if (w) {
        HitZone zone = hit_test(w, mx, my);
        if (hover_window != w || hover_zone != zone) {
            hover_window = w;
            hover_zone = zone;
        }

        // Set cursor style based on zone
        switch (zone) {
            case HIT_BORDER_N:
            case HIT_BORDER_S:
                gui::cursor::set_style(2); // vertical resize
                break;
            case HIT_BORDER_E:
            case HIT_BORDER_W:
                gui::cursor::set_style(3); // horizontal resize
                break;
            case HIT_BORDER_NW:
            case HIT_BORDER_SE:
                gui::cursor::set_style(4); // diagonal NW-SE
                break;
            case HIT_BORDER_NE:
            case HIT_BORDER_SW:
                gui::cursor::set_style(5); // diagonal NE-SW
                break;
            default:
                gui::cursor::set_style(0); // default arrow
                break;
        }
    } else {
        hover_window = nullptr;
        hover_zone = HIT_NONE;
        gui::cursor::set_style(0);
    }
}

// ---------------------------------------------------------------------------
// Drag / resize handling
// ---------------------------------------------------------------------------

void begin_drag(Window* w, int32 mx, int32 my) {
    if (!w || !w->active) return;

    HitZone zone = hit_test(w, mx, my);

    switch (zone) {
        case HIT_CLOSE:
            destroy(w);
            return;

        case HIT_MIN:
            minimize(w);
            return;

        case HIT_MAX:
            if (w->flags & WF_MAXIMIZED) {
                restore(w);
            } else {
                maximize(w);
            }
            return;

        case HIT_TITLE:
            // If maximized, restore first then adjust offset
            if (w->flags & WF_MAXIMIZED) {
                restore(w);
                // Center window on cursor
                w->frame.x = mx - (int32)w->frame.w / 2;
                w->frame.y = my - (int32)TITLE_BAR_HEIGHT / 2;
                update_content_rect(w);
            }
            drag_window = w;
            drag_mode = HIT_TITLE;
            drag_offset_x = mx - w->frame.x;
            drag_offset_y = my - w->frame.y;
            set_focused(w);
            break;

        case HIT_CONTENT:
            set_focused(w);
            break;

        case HIT_BORDER_N:
        case HIT_BORDER_S:
        case HIT_BORDER_E:
        case HIT_BORDER_W:
        case HIT_BORDER_NE:
        case HIT_BORDER_NW:
        case HIT_BORDER_SE:
        case HIT_BORDER_SW:
            if (w->flags & WF_RESIZABLE) {
                drag_window = w;
                drag_mode = zone;
                resize_start_x = mx;
                resize_start_y = my;
                resize_origin = w->frame;
                set_focused(w);
            }
            break;

        default:
            break;
    }
}

void update_drag(int32 mx, int32 my) {
    if (!drag_window) return;

    if (drag_mode == HIT_TITLE) {
        // Move window
        int32 nx = mx - drag_offset_x;
        int32 ny = my - drag_offset_y;
        // Clamp so title bar stays accessible
        if (ny < 0) ny = 0;
        if (ny > (int32)gui::screen_height() - (int32)TITLE_BAR_HEIGHT) {
            ny = (int32)gui::screen_height() - (int32)TITLE_BAR_HEIGHT;
        }
        move(drag_window, nx, ny);
        return;
    }

    // Resize operations
    int32 dx = mx - resize_start_x;
    int32 dy = my - resize_start_y;
    int32 nx = resize_origin.x;
    int32 ny = resize_origin.y;
    int32 nw = (int32)resize_origin.w;
    int32 nh = (int32)resize_origin.h;

    switch (drag_mode) {
        case HIT_BORDER_E:
            nw += dx;
            break;
        case HIT_BORDER_W:
            nx += dx;
            nw -= dx;
            break;
        case HIT_BORDER_S:
            nh += dy;
            break;
        case HIT_BORDER_N:
            ny += dy;
            nh -= dy;
            break;
        case HIT_BORDER_SE:
            nw += dx;
            nh += dy;
            break;
        case HIT_BORDER_SW:
            nx += dx;
            nw -= dx;
            nh += dy;
            break;
        case HIT_BORDER_NE:
            nw += dx;
            ny += dy;
            nh -= dy;
            break;
        case HIT_BORDER_NW:
            nx += dx;
            nw -= dx;
            ny += dy;
            nh -= dy;
            break;
        default:
            break;
    }

    if (nw < (int32)MIN_WIN_W) nw = (int32)MIN_WIN_W;
    if (nh < (int32)MIN_WIN_H) nh = (int32)MIN_WIN_H;

    drag_window->frame.x = nx;
    drag_window->frame.y = ny;
    drag_window->frame.w = (uint32)nw;
    drag_window->frame.h = (uint32)nh;
    update_content_rect(drag_window);
    drag_window->dirty = true;
}

void end_drag() {
    drag_window = nullptr;
    drag_mode = HIT_NONE;
}

bool is_dragging() {
    return drag_window != nullptr;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

uint32 get_window_count() {
    return window_count;
}

Window* get_window(uint32 index) {
    if (index >= window_count) return nullptr;
    return &windows[z_order[index]];
}

Window* find_by_id(uint32 id) {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].id == id) {
            return &windows[i];
        }
    }
    return nullptr;
}

Window* find_by_app(uint32 app_id) {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].app_id == app_id) {
            return &windows[i];
        }
    }
    return nullptr;
}

void set_app_id(Window* w, uint32 app_id) {
    if (w && w->active) {
        w->app_id = app_id;
    }
}

uint32 get_flags(Window* w) {
    if (!w) return 0;
    return w->flags;
}

void set_title(Window* w, const char* title) {
    if (w && w->active) {
        str_copy(w->title, title, 64);
        w->dirty = true;
    }
}

Rect get_frame(Window* w) {
    if (!w) return { 0, 0, 0, 0 };
    return w->frame;
}

Rect get_content_rect(Window* w) {
    if (!w) return { 0, 0, 0, 0 };
    return w->content;
}

// ---------------------------------------------------------------------------
// Iteration helpers
// ---------------------------------------------------------------------------

void for_each_visible(void (*callback)(Window*)) {
    for (uint32 i = 0; i < window_count; i++) {
        Window* w = &windows[z_order[i]];
        if (w->active && (w->flags & WF_VISIBLE) && !(w->flags & WF_MINIMIZED)) {
            callback(w);
        }
    }
}

void for_each(void (*callback)(Window*)) {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) {
            callback(&windows[i]);
        }
    }
}

void mark_all_dirty() {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) {
            windows[i].dirty = true;
        }
    }
}

bool any_dirty() {
    for (uint32 i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].dirty) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Snap to edges (Aero Snap style)
// ---------------------------------------------------------------------------

void snap_left(Window* w) {
    if (!w || !w->active) return;
    w->restore_frame = w->frame;
    w->frame.x = 0;
    w->frame.y = 0;
    w->frame.w = gui::screen_width() / 2;
    w->frame.h = gui::screen_height() - 40; // leave taskbar space
    w->flags &= ~WF_MAXIMIZED;
    update_content_rect(w);
    w->dirty = true;
}

void snap_right(Window* w) {
    if (!w || !w->active) return;
    w->restore_frame = w->frame;
    uint32 half = gui::screen_width() / 2;
    w->frame.x = (int32)half;
    w->frame.y = 0;
    w->frame.w = gui::screen_width() - half;
    w->frame.h = gui::screen_height() - 40;
    w->flags &= ~WF_MAXIMIZED;
    update_content_rect(w);
    w->dirty = true;
}

// ---------------------------------------------------------------------------
// Cascade / tile helpers
// ---------------------------------------------------------------------------

void cascade_all() {
    int32 offset = 0;
    for (uint32 i = 0; i < window_count; i++) {
        Window* w = &windows[z_order[i]];
        if (!w->active || (w->flags & WF_MINIMIZED)) continue;
        w->frame.x = 30 + offset;
        w->frame.y = 30 + offset;
        w->frame.w = 500;
        w->frame.h = 400;
        w->flags &= ~WF_MAXIMIZED;
        update_content_rect(w);
        w->dirty = true;
        offset += 30;
    }
}

void tile_all() {
    // Count visible non-minimized windows
    uint32 visible = 0;
    for (uint32 i = 0; i < window_count; i++) {
        Window* w = &windows[z_order[i]];
        if (w->active && !(w->flags & WF_MINIMIZED)) visible++;
    }
    if (visible == 0) return;

    uint32 cols = 1;
    while (cols * cols < visible) cols++;
    uint32 rows = (visible + cols - 1) / cols;

    uint32 sw = gui::screen_width();
    uint32 sh = gui::screen_height() - 40; // taskbar
    uint32 tile_w = sw / cols;
    uint32 tile_h = sh / rows;

    uint32 idx = 0;
    for (uint32 i = 0; i < window_count && idx < visible; i++) {
        Window* w = &windows[z_order[i]];
        if (!w->active || (w->flags & WF_MINIMIZED)) continue;
        uint32 col = idx % cols;
        uint32 row = idx / cols;
        w->frame.x = (int32)(col * tile_w);
        w->frame.y = (int32)(row * tile_h);
        w->frame.w = tile_w;
        w->frame.h = tile_h;
        w->flags &= ~WF_MAXIMIZED;
        update_content_rect(w);
        w->dirty = true;
        idx++;
    }
}

} // namespace wm
} // namespace neo
