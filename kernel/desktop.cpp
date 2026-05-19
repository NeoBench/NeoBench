/*
 * NeoBench OS — Desktop Environment
 * kernel/desktop.cpp
 *
 * Main desktop: wallpaper, icons, context menus, and the
 * primary event loop that dispatches to WM / taskbar / apps.
 */

#include "gui.h"
#include "neobench.h"

namespace neo { namespace desktop {

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void handle_mouse_input();
static void handle_keyboard_input();
static void draw_desktop_icons();
static void draw_context_menu();
static void draw_wallpaper_submenu();
static void select_icon_at(int32 mx, int32 my);
static void launch_icon(uint16 idx);
static void open_context_menu(int32 x, int32 y);
static void close_context_menu();
static void handle_context_click(int32 mx, int32 my);
static void handle_wallpaper_submenu_click(int32 mx, int32 my);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const uint16 ICON_GRID_X     = 80;
static const uint16 ICON_GRID_Y     = 80;
static const uint16 ICON_START_X    = 24;
static const uint16 ICON_START_Y    = 16;
static const uint16 ICON_SIZE       = 32;
static const uint16 ICON_LABEL_GAP  = 4;
static const uint16 DBLCLICK_FRAMES = 24;     // ~400 ms at 60 fps

static const uint16 CTX_MENU_W      = 200;
static const uint16 CTX_ITEM_H      = 24;
static const uint16 CTX_SEP_H       = 8;
static const uint16 WALLPAPER_SUB_W = 160;

// ---------------------------------------------------------------------------
// Desktop icon definition
// ---------------------------------------------------------------------------
struct DesktopIcon {
    const char*      label;
    gui::icon::IconId icon_id;
    uint32           app_id;
    bool             selected;
    int32            x, y;           // computed position
};

static DesktopIcon s_icons[] = {
    { "File Manager",    gui::icon::ICON_FOLDER,     1, false, 0, 0 },
    { "System Monitor",  gui::icon::ICON_APP,    2, false, 0, 0 },
    { "NeoEdit",         gui::icon::ICON_FILE,   3, false, 0, 0 },
    { "NeoBrowse",       gui::icon::ICON_NETWORK,      4, false, 0, 0 },
    { "Terminal",        gui::icon::ICON_TERMINAL,   5, false, 0, 0 },
    { "Trash",           gui::icon::ICON_DELETE,      0, false, 0, 0 },
};
static const uint16 NUM_ICONS = sizeof(s_icons) / sizeof(s_icons[0]);

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------
enum CtxMenuItem {
    CTX_REFRESH = 0,
    CTX_WALLPAPER,
    CTX_DISPLAY,
    CTX_NEW_FOLDER,
    CTX_SEPARATOR,
    CTX_ABOUT
};

struct CtxEntry {
    const char*  label;
    CtxMenuItem  id;
    bool         is_separator;
};

static const CtxEntry s_ctx_items[] = {
    { "Refresh Desktop",     CTX_REFRESH,    false },
    { "Change Wallpaper  \xbb", CTX_WALLPAPER,  false },
    { "Display Settings",    CTX_DISPLAY,    false },
    { "New Folder",          CTX_NEW_FOLDER, false },
    { "",                    CTX_SEPARATOR,  true  },
    { "About NeoBench",      CTX_ABOUT,      false },
};
static const uint16 NUM_CTX_ITEMS = sizeof(s_ctx_items) / sizeof(s_ctx_items[0]);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static gui::GfxMode      s_gfx_mode;
static gui::wallpaper::Type s_wallpaper_type;
static uint8*             s_wallpaper_buf    = nullptr;
static uint32             s_wallpaper_size   = 0;
static uint16             s_screen_w         = 0;
static uint16             s_screen_h         = 0;

// Mouse state (hardware-read, accumulated)
static int32  s_mouse_x  = 0;
static int32  s_mouse_y  = 0;
static bool   s_lmb      = false;
static bool   s_rmb      = false;
static bool   s_lmb_prev = false;
static bool   s_rmb_prev = false;
static uint16 s_prev_joy = 0;

// Double-click tracking
static uint32 s_last_click_frame = 0;
static int32  s_last_click_x     = 0;
static int32  s_last_click_y     = 0;
static uint32 s_frame_counter    = 0;

// Context menu
static bool   s_ctx_open      = false;
static int32  s_ctx_x         = 0;
static int32  s_ctx_y         = 0;
static bool   s_wallpaper_sub = false;

// Window dragging
static bool   s_dragging      = false;

// Running flag
static bool   s_running       = true;

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------
static gui::Color col_white()     { return gui::Color{255,255,255,255}; }
static gui::Color col_black()     { return gui::Color{0,0,0,255}; }
static gui::Color col_select()    { return gui::Color{60,120,220,160}; }
static gui::Color col_ctx_bg()    { return gui::Color{30,30,40,210}; }
static gui::Color col_ctx_hover() { return gui::Color{60,100,180,200}; }
static gui::Color col_ctx_bdr()   { return gui::Color{100,100,120,220}; }
static gui::Color col_ctx_text()  { return gui::Color{230,230,230,255}; }
static gui::Color col_sep()       { return gui::Color{80,80,100,180}; }
static gui::Color col_icon_text() { return gui::Color{255,255,255,255}; }
static gui::Color col_shadow()    { return gui::Color{0,0,0,128}; }

// ---------------------------------------------------------------------------
// Wallpaper management
// ---------------------------------------------------------------------------
static void regenerate_wallpaper() {
    uint16 bpp = (gui::get_mode() == gui::GFX_RTG) ? 2 : 1;
    uint32 needed = (uint32)s_screen_w * s_screen_h * bpp;
    if (s_wallpaper_buf == nullptr || s_wallpaper_size < needed) {
        if (s_wallpaper_buf) neo::memory::free(s_wallpaper_buf);
        s_wallpaper_buf  = (uint8*)neo::memory::alloc(needed, 0);
        s_wallpaper_size = needed;
    }
    gui::wallpaper::generate(s_wallpaper_type, s_wallpaper_buf,
                             s_screen_w, s_screen_h, bpp);
}

void set_wallpaper(gui::wallpaper::Type type) {
    s_wallpaper_type = type;
    regenerate_wallpaper();
}

gui::wallpaper::Type get_wallpaper() {
    return s_wallpaper_type;
}

// ---------------------------------------------------------------------------
// Draw wallpaper — blit pre-rendered buffer
// ---------------------------------------------------------------------------
static void draw_wallpaper() {
    if (s_wallpaper_buf) {
        // Blit entire wallpaper buffer to framebuffer
        uint32 wp_pitch = (gui::get_mode() == gui::GFX_RTG)
                          ? (uint32)s_screen_w * 2
                          : (uint32)s_screen_w;
        gui::blit(s_wallpaper_buf, wp_pitch, 0, 0, s_screen_w, s_screen_h);
    } else {
        gui::fill_rect(gui::Rect{0, 0, (int32)s_screen_w, (int32)s_screen_h},
                        gui::Color{20, 40, 80, 255});
    }
}

// ---------------------------------------------------------------------------
// Compute icon grid positions
// ---------------------------------------------------------------------------
static void compute_icon_positions() {
    uint16 taskbar_h = neo::taskbar::get_taskbar_height();
    uint16 usable_h  = s_screen_h - taskbar_h;
    uint16 max_rows  = (usable_h - ICON_START_Y) / ICON_GRID_Y;
    if (max_rows < 1) max_rows = 1;

    for (uint16 i = 0; i < NUM_ICONS; ++i) {
        uint16 col = i / max_rows;
        uint16 row = i % max_rows;
        s_icons[i].x = ICON_START_X + col * ICON_GRID_X;
        s_icons[i].y = ICON_START_Y + row * ICON_GRID_Y;
    }
}

// ---------------------------------------------------------------------------
// Draw desktop icons
// ---------------------------------------------------------------------------
static void draw_desktop_icons() {
    for (uint16 i = 0; i < NUM_ICONS; ++i) {
        DesktopIcon& ic = s_icons[i];
        int32 ix = ic.x;
        int32 iy = ic.y;

        // Selection highlight
        if (ic.selected) {
            gui::Rect sel = { ix - 4, iy - 4, ICON_SIZE + 8, ICON_SIZE + 24 };
            gui::alpha_blend(sel, col_select());
        }

        // Icon
        gui::icon::draw_icon(ix, iy, ic.icon_id);

        // Label (centered below icon)
        int tw = gui::text_width(ic.label, FontSize::Small);
        int lx = ix + (ICON_SIZE - tw) / 2;
        int ly = iy + ICON_SIZE + ICON_LABEL_GAP;

        // Drop shadow
        gui::draw_text(lx + 1, ly + 1, ic.label, col_shadow(), FontSize::Small);
        gui::draw_text(lx, ly, ic.label, col_icon_text(), FontSize::Small);
    }
}

// ---------------------------------------------------------------------------
// Icon hit-testing
// ---------------------------------------------------------------------------
static int16 icon_at(int32 mx, int32 my) {
    for (uint16 i = 0; i < NUM_ICONS; ++i) {
        int32 ix = s_icons[i].x - 4;
        int32 iy = s_icons[i].y - 4;
        int32 iw = ICON_SIZE + 8;
        int32 ih = ICON_SIZE + 24;
        if (mx >= ix && mx < ix + iw && my >= iy && my < iy + ih) {
            return (int16)i;
        }
    }
    return -1;
}

static void deselect_all_icons() {
    for (uint16 i = 0; i < NUM_ICONS; ++i)
        s_icons[i].selected = false;
}

static void select_icon_at(int32 mx, int32 my) {
    deselect_all_icons();
    int16 idx = icon_at(mx, my);
    if (idx >= 0) {
        s_icons[idx].selected = true;
    }
}

static void launch_icon(uint16 idx) {
    if (idx < NUM_ICONS && s_icons[idx].app_id > 0) {
        ////launch_app(s_icons[idx].app_id);
    }
}

// ---------------------------------------------------------------------------
// Context menu drawing
// ---------------------------------------------------------------------------
static uint16 ctx_menu_height() {
    uint16 h = 8; // padding
    for (uint16 i = 0; i < NUM_CTX_ITEMS; ++i) {
        h += s_ctx_items[i].is_separator ? CTX_SEP_H : CTX_ITEM_H;
    }
    return h;
}

static void draw_context_menu() {
    if (!s_ctx_open) return;

    uint16 mh = ctx_menu_height();
    gui::Rect bg = { s_ctx_x, s_ctx_y, CTX_MENU_W, mh };

    // Glass background
    gui::alpha_blend(bg, col_ctx_bg());
    gui::draw_rect(bg, col_ctx_bdr());

    int32 iy = s_ctx_y + 4;
    for (uint16 i = 0; i < NUM_CTX_ITEMS; ++i) {
        if (s_ctx_items[i].is_separator) {
            int32 sy = iy + CTX_SEP_H / 2;
            gui::fill_rect(gui::Rect{s_ctx_x + 8, sy, CTX_MENU_W - 16, 1}, col_sep());
            iy += CTX_SEP_H;
            continue;
        }

        // Hover highlight
        if (s_mouse_x >= s_ctx_x && s_mouse_x < s_ctx_x + CTX_MENU_W &&
            s_mouse_y >= iy && s_mouse_y < iy + CTX_ITEM_H) {
            gui::Rect hover = { s_ctx_x + 2, iy, CTX_MENU_W - 4, CTX_ITEM_H };
            gui::alpha_blend(hover, col_ctx_hover());
        }

        gui::draw_text(s_ctx_x + 12, iy + 4, s_ctx_items[i].label,
                        col_ctx_text(), FontSize::Normal);
        iy += CTX_ITEM_H;
    }

    // Wallpaper submenu
    if (s_wallpaper_sub) {
        draw_wallpaper_submenu();
    }
}

// ---------------------------------------------------------------------------
// Wallpaper submenu
// ---------------------------------------------------------------------------
static void draw_wallpaper_submenu() {
    int32 sub_x = s_ctx_x + CTX_MENU_W - 2;
    // wallpaper item is index 1 — offset it
    int32 sub_y = s_ctx_y + 4 + CTX_ITEM_H;

    uint16 num_types = 6;
    uint16 sub_h = num_types * CTX_ITEM_H + 8;

    gui::Rect bg = { sub_x, sub_y, WALLPAPER_SUB_W, sub_h };
    gui::alpha_blend(bg, col_ctx_bg());
    gui::draw_rect(bg, col_ctx_bdr());

    int32 iy = sub_y + 4;
    for (uint16 t = 0; t < num_types; ++t) {
        const char* name = gui::wallpaper::get_name((gui::wallpaper::Type)t);

        // Hover
        if (s_mouse_x >= sub_x && s_mouse_x < sub_x + WALLPAPER_SUB_W &&
            s_mouse_y >= iy && s_mouse_y < iy + CTX_ITEM_H) {
            gui::Rect hover = { sub_x + 2, iy, WALLPAPER_SUB_W - 4, CTX_ITEM_H };
            gui::alpha_blend(hover, col_ctx_hover());
        }

        // Current indicator
        gui::Color tc = col_ctx_text();
        if ((gui::wallpaper::Type)t == s_wallpaper_type) {
            tc = gui::Color{100, 200, 255, 255};
        }

        gui::draw_text(sub_x + 12, iy + 4, name, tc, FontSize::Normal);
        iy += CTX_ITEM_H;
    }
}

// ---------------------------------------------------------------------------
// Context menu interaction
// ---------------------------------------------------------------------------
static void open_context_menu(int32 x, int32 y) {
    s_ctx_open      = true;
    s_wallpaper_sub = false;
    s_ctx_x         = x;
    s_ctx_y         = y;

    // Clamp to screen
    uint16 mh = ctx_menu_height();
    if (s_ctx_x + CTX_MENU_W > s_screen_w) s_ctx_x = s_screen_w - CTX_MENU_W - 4;
    if (s_ctx_y + mh > s_screen_h)          s_ctx_y = s_screen_h - mh - 4;
    if (s_ctx_x < 0) s_ctx_x = 0;
    if (s_ctx_y < 0) s_ctx_y = 0;
}

static void close_context_menu() {
    s_ctx_open      = false;
    s_wallpaper_sub = false;
}

static bool point_in_ctx_menu(int32 mx, int32 my) {
    if (!s_ctx_open) return false;
    uint16 mh = ctx_menu_height();
    if (mx >= s_ctx_x && mx < s_ctx_x + CTX_MENU_W &&
        my >= s_ctx_y && my < s_ctx_y + mh)
        return true;
    // Check submenu
    if (s_wallpaper_sub) {
        int32 sub_x = s_ctx_x + CTX_MENU_W - 2;
        int32 sub_y = s_ctx_y + 4 + CTX_ITEM_H;
        uint16 sub_h = 6 * CTX_ITEM_H + 8;
        if (mx >= sub_x && mx < sub_x + WALLPAPER_SUB_W &&
            my >= sub_y && my < sub_y + sub_h)
            return true;
    }
    return false;
}

static void handle_context_click(int32 mx, int32 my) {
    // Check wallpaper submenu first
    if (s_wallpaper_sub) {
        handle_wallpaper_submenu_click(mx, my);
    }

    int32 iy = s_ctx_y + 4;
    for (uint16 i = 0; i < NUM_CTX_ITEMS; ++i) {
        if (s_ctx_items[i].is_separator) {
            iy += CTX_SEP_H;
            continue;
        }
        if (mx >= s_ctx_x && mx < s_ctx_x + CTX_MENU_W &&
            my >= iy && my < iy + CTX_ITEM_H) {

            switch (s_ctx_items[i].id) {
                case CTX_REFRESH:
                    regenerate_wallpaper();
                    close_context_menu();
                    break;
                case CTX_WALLPAPER:
                    s_wallpaper_sub = !s_wallpaper_sub;
                    return; // don't close
                case CTX_DISPLAY:
                    ////launch_app(20); // display settings app id
                    close_context_menu();
                    break;
                case CTX_NEW_FOLDER:
                    // TODO: implement new folder creation
                    close_context_menu();
                    break;
                case CTX_ABOUT:
                    ////launch_app(30); // about dialog app id
                    close_context_menu();
                    break;
                default:
                    close_context_menu();
                    break;
            }
            return;
        }
        iy += CTX_ITEM_H;
    }
    close_context_menu();
}

static void handle_wallpaper_submenu_click(int32 mx, int32 my) {
    int32 sub_x = s_ctx_x + CTX_MENU_W - 2;
    int32 sub_y = s_ctx_y + 4 + CTX_ITEM_H;

    if (mx < sub_x || mx >= sub_x + WALLPAPER_SUB_W) return;

    int32 iy = sub_y + 4;
    for (uint16 t = 0; t < 6; ++t) {
        if (my >= iy && my < iy + CTX_ITEM_H) {
            set_wallpaper((gui::wallpaper::Type)t);
            close_context_menu();
            return;
        }
        iy += CTX_ITEM_H;
    }
}

// ---------------------------------------------------------------------------
// Mouse hardware reading
// ---------------------------------------------------------------------------
static void read_mouse_hardware() {
    volatile uint16* JOY0DAT  = (volatile uint16*)0xDFF00A;
    volatile uint8*  CIAA_PRA = (volatile uint8*)0xBFE001;
    volatile uint16* POTGOR   = (volatile uint16*)0xDFF016;

    uint16 joy = *JOY0DAT;

    // Compute deltas using counter method
    int16 dx = (int16)((int8)((joy & 0xFF) - (s_prev_joy & 0xFF)));
    int16 dy = (int16)((int8)(((joy >> 8) & 0xFF) - ((s_prev_joy >> 8) & 0xFF)));
    s_prev_joy = joy;

    s_mouse_x += dx;
    s_mouse_y += dy;

    // Clamp to screen
    if (s_mouse_x < 0) s_mouse_x = 0;
    if (s_mouse_y < 0) s_mouse_y = 0;
    if (s_mouse_x >= s_screen_w) s_mouse_x = s_screen_w - 1;
    if (s_mouse_y >= s_screen_h) s_mouse_y = s_screen_h - 1;

    // Buttons
    s_lmb_prev = s_lmb;
    s_rmb_prev = s_rmb;
    s_lmb = !(*CIAA_PRA & (1 << 6));       // active low
    s_rmb = !(*POTGOR   & (1 << 10));       // active low
}

// ---------------------------------------------------------------------------
// Mouse event dispatch
// ---------------------------------------------------------------------------
static void handle_mouse_input() {
    read_mouse_hardware();

    bool lmb_down = s_lmb && !s_lmb_prev;  // just pressed
    bool lmb_up   = !s_lmb && s_lmb_prev;  // just released
    bool rmb_down = s_rmb && !s_rmb_prev;

    // Handle ongoing drag
    if (s_dragging) {
        if (s_lmb) {
            ////neo::wm::update_drag(s_mouse_x, s_mouse_y);
        } else {
            ////neo::wm::end_drag();
            s_dragging = false;
        }
        return;
    }

    // Left button pressed
    if (lmb_down) {
        // Context menu — check if clicking inside it
        if (s_ctx_open) {
            if (point_in_ctx_menu(s_mouse_x, s_mouse_y)) {
                handle_context_click(s_mouse_x, s_mouse_y);
                return;
            }
            close_context_menu();
            return;
        }

        // Start menu
        if (false &&
            false) {
            uint32 app = 0;
            if (app > 0) ////launch_app(app);
            return;
        }

        // Taskbar
        if (false) {
            uint32 app = 0;
            if (app > 0) ////launch_app(app);
            return;
        }

        // Window hit test
        void* win = nullptr;
        if (win) {
            ////neo::wm::bring_to_front(win);
            ////neo::wm::set_focused(win);

            int zone = 0;
            switch (zone) {
                    ////neo::wm::destroy(win);
                    break;
                    ////neo::wm::minimize(win);
                    break;
                    if (win->flags & neo::wm::0)
                        ////neo::wm::restore(win);
                    else
                        ////neo::wm::maximize(win);
                    break;
                    ////neo::wm::begin_drag(win, s_mouse_x, s_mouse_y);
                    s_dragging = true;
                    break;
                    if (win->flags & neo::wm::0) {
                        ////neo::wm::begin_drag(win, s_mouse_x, s_mouse_y);
                        s_dragging = true;
                    }
                    break;
                default:
                    break;
            }
            return;
        }

        // Desktop icon click / double-click
        int16 idx = icon_at(s_mouse_x, s_mouse_y);
        if (idx >= 0) {
            // Check double-click
            bool dblclick = false;
            if (s_icons[idx].selected &&
                (s_frame_counter - s_last_click_frame) < DBLCLICK_FRAMES) {
                int32 ddx = s_mouse_x - s_last_click_x;
                int32 ddy = s_mouse_y - s_last_click_y;
                if (ddx > -4 && ddx < 4 && ddy > -4 && ddy < 4)
                    dblclick = true;
            }

            select_icon_at(s_mouse_x, s_mouse_y);
            s_last_click_frame = s_frame_counter;
            s_last_click_x     = s_mouse_x;
            s_last_click_y     = s_mouse_y;

            if (dblclick) {
                launch_icon((uint16)idx);
            }
            return;
        }

        // Click on empty desktop — deselect icons, close start menu
        deselect_all_icons();
        if (false)
            //startmenu;
    }

    // Right button pressed
    if (rmb_down) {
        if (s_ctx_open) {
            close_context_menu();
            return;
        }

        // Right-click on window title bar → window context menu (use generic ctx for now)
        void* win = nullptr;
        if (win) {
            int zone = 0;
                // Could show window-specific context menu — use desktop menu
                open_context_menu(s_mouse_x, s_mouse_y);
                return;
            }
        }

        // Right-click on desktop → context menu
        if (!false) {
            open_context_menu(s_mouse_x, s_mouse_y);
        }
    }
}

// ---------------------------------------------------------------------------
// Keyboard handling
// ---------------------------------------------------------------------------
static void handle_keyboard_input() {
    if (!neo::keyboard::key_available()) return;
    uint8 scancode = neo::keyboard::read_scancode();
    if (scancode == 0) return;

    bool key_down = !(scancode & 0x80);
    uint8 key     = scancode & 0x7F;

    // Track modifier state
    static bool alt_held  = false;
    static bool lami_held = false;  // Left Amiga (Win key equivalent)

    if (key == 0x64 || key == 0x65) {  // Left/Right Alt
        alt_held = key_down;
        return;
    }
    if (key == 0x66) {  // Left Amiga
        lami_held = key_down;
        if (key_down) {
            //startmenu;
        }
        return;
    }

    if (!key_down) return;  // Only process key-down events below

    // Alt+F4 — close focused window
    if (alt_held && key == 0x53) {  // F4 = 0x53
        void* focused = nullptr;
        if (focused) {
            ////neo::wm::destroy(focused);
        }
        return;
    }

    // Alt+Tab — cycle windows
    if (alt_held && key == 0x42) {  // Tab = 0x42
        void* focused = nullptr;
        // Simple cycle: find next visible window
        // The WM should expose iteration, but we can use find_at as fallback
        // For now, the WM would handle the actual cycling logic
        (void)focused;
        return;
    }

    // ESC — close context menu or start menu
    if (key == 0x45) {
        if (s_ctx_open) close_context_menu();
        else if (false)
            //startmenu;
    }
}

// ---------------------------------------------------------------------------
// launch_app — create window for an application
// ---------------------------------------------------------------------------
void ////launch_app(uint32 app_id) {
    const char* title = "Application";
    uint16 w = 400, h = 300;
    uint16 x = 60 + (app_id * 30) % 200;
    uint16 y = 40 + (app_id * 25) % 150;

    switch (app_id) {
        case 1:  title = "File Manager";    w = 500; h = 380; break;
        case 2:  title = "System Monitor";  w = 420; h = 320; break;
        case 3:  title = "NeoEdit";         w = 480; h = 360; break;
        case 4:  title = "NeoBrowse";       w = 560; h = 400; break;
        case 5:  title = "Terminal";         w = 480; h = 300; break;
        case 20: title = "Display Settings"; w = 380; h = 280; break;
        case 30: title = "About NeoBench";  w = 320; h = 240; break;
        default: break;
    }

    uint32 flags = neo::wm::0 | neo::wm::0;
    void* win = nullptr;
    if (win) {
        win->app_id = app_id;
        //addtask;
        ////neo::wm::set_focused(win);
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void init(gui::GfxMode mode, bool first_boot) {
    s_gfx_mode = mode;
    s_screen_w = gui::screen_width();
    s_screen_h = gui::screen_height();

    s_mouse_x = s_screen_w / 2;
    s_mouse_y = s_screen_h / 2;
    s_lmb = s_rmb = s_lmb_prev = s_rmb_prev = false;
    s_prev_joy = *((volatile uint16*)0xDFF00A);

    s_ctx_open      = false;
    s_wallpaper_sub = false;
    s_dragging      = false;
    s_running       = true;
    s_frame_counter = 0;

    // Default wallpaper
    if (first_boot) {
        s_wallpaper_type = (gui::wallpaper::Type)0;
    }
    regenerate_wallpaper();

    compute_icon_positions();

    // Init subsystems
    neo::wm::init();
    neo::taskbar::init();
}

// ---------------------------------------------------------------------------
// run — main desktop event loop (does not return normally)
// ---------------------------------------------------------------------------
void run() {
    while (s_running) {
        // --- Input ---
        handle_mouse_input();
        handle_keyboard_input();

        // --- Render ---
        gui::begin_frame();

        // 1. Wallpaper
        draw_wallpaper();

        // 2. Desktop icons
        draw_desktop_icons();

        // 3. All windows
        neo::wm::draw_all();

        // 4. Taskbar + start menu
        neo::taskbar::draw();
        if (false) {
            //drawstart;
        }

        // 5. Context menu
        if (s_ctx_open) {
            draw_context_menu();
        }

        // 6. Mouse cursor (always on top)
        gui::cursor::draw(s_mouse_x, s_mouse_y);

        gui::end_frame();

        s_frame_counter++;
    }
}

// ---------------------------------------------------------------------------
// shutdown / restart
// ---------------------------------------------------------------------------
void shutdown() {
    s_running = false;
    if (s_wallpaper_buf) {
        neo::memory::free(s_wallpaper_buf);
        s_wallpaper_buf = nullptr;
    }
    
}

void restart() {
    s_running = false;
    if (s_wallpaper_buf) {
        neo::memory::free(s_wallpaper_buf);
        s_wallpaper_buf = nullptr;
    }
    // Trigger warm reset via keyboard controller
    volatile uint8* CIAA_CRA = (volatile uint8*)0xBFE801;
    *CIAA_CRA = 0x00;
    // Jump to ROM reset vector
    typedef void (*ResetFunc)();
    ResetFunc reset = (ResetFunc)0x00FC0002;
    reset();
}

}} // namespace neo::desktop
