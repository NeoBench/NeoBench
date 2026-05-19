/*
 * NeoBench Kernel — Taskbar & Start Menu
 * Vista/Aero-style glass taskbar with full Start menu
 * Target: M68K (68030/040/060) bare-metal Amiga
 */

#include "gui.h"
#include "neobench.h"

// Forward declaration — window manager
namespace neo { namespace wm {
    struct Window;
    uint32 get_window_count();
    Window* get_window(uint32 index);
    void set_focused(Window* w);
    void restore(Window* w);
    uint32 get_flags(Window* w);
    Rect get_frame(Window* w);
}}

namespace neo {
namespace taskbar {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const uint32 TASKBAR_HEIGHT      = 40;
static const uint32 START_BTN_WIDTH     = 100;
static const uint32 TASK_BTN_WIDTH      = 150;
static const uint32 TASK_BTN_HEIGHT     = 30;
static const uint32 TASK_BTN_MARGIN     = 4;
static const uint32 CLOCK_WIDTH         = 80;
static const uint32 TRAY_PADDING        = 8;

static const uint32 MENU_WIDTH          = 320;
static const uint32 MENU_ITEM_HEIGHT    = 28;
static const uint32 MENU_HEADER_HEIGHT  = 32;
static const uint32 MENU_SIDEBAR_WIDTH  = 70;
static const uint32 MENU_BOTTOM_HEIGHT  = 44;
static const uint32 MENU_PADDING        = 6;

static const uint32 MAX_TASKS           = 32;
static const uint32 NUM_CATEGORIES      = 7;
static const uint32 MAX_APPS_PER_CAT    = 10;

// Window flags (must match window_mgr.cpp)
static const uint32 WF_VISIBLE   = 0x0001;
static const uint32 WF_MINIMIZED = 0x0002;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

static const Color COL_BAR_BG          = {  15,  20,  30, 220 };
static const Color COL_BAR_HIGHLIGHT   = {  80, 120, 180, 120 };
static const Color COL_BAR_TOP_BORDER  = { 100, 140, 200, 150 };
static const Color COL_GLASS           = {  40,  60,  80, 160 };
static const Color COL_START_BG        = {  30,  70, 130, 220 };
static const Color COL_START_HOVER     = {  50,  90, 160, 240 };
static const Color COL_TASK_BG         = {  40,  50,  70, 180 };
static const Color COL_TASK_HOVER      = {  60,  80, 120, 200 };
static const Color COL_TASK_FOCUSED    = {  70, 100, 150, 220 };
static const Color COL_TEXT_WHITE      = { 255, 255, 255, 255 };
static const Color COL_TEXT_GRAY       = { 180, 190, 200, 255 };
static const Color COL_CLOCK_TEXT      = { 220, 230, 240, 255 };
static const Color COL_MENU_BG        = {  20,  28,  40, 235 };
static const Color COL_MENU_GLASS     = {  30,  50,  70, 180 };
static const Color COL_MENU_SIDEBAR   = {  10,  14,  22, 240 };
static const Color COL_MENU_ITEM_HOVER= {  50,  80, 130, 200 };
static const Color COL_MENU_HEADER    = { 160, 190, 220, 255 };
static const Color COL_MENU_ITEM_TEXT = { 230, 235, 240, 255 };
static const Color COL_MENU_BORDER    = {  80, 110, 160, 180 };
static const Color COL_SHUTDOWN_BG    = {  60,  30,  30, 220 };
static const Color COL_SHUTDOWN_HOVER = { 180,  50,  50, 240 };

// ---------------------------------------------------------------------------
// App definition
// ---------------------------------------------------------------------------

struct AppEntry {
    const char* name;
    uint32      icon_id;    // gui::icon::IconId
    uint32      app_id;     // unique ID for launching
};

struct Category {
    const char* name;
    uint32      app_count;
    AppEntry    apps[MAX_APPS_PER_CAT];
    bool        expanded;
};

// ---------------------------------------------------------------------------
// Task entry (linked to window)
// ---------------------------------------------------------------------------

struct TaskEntry {
    neo::wm::Window* window;
    bool             active;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static TaskEntry tasks[MAX_TASKS];
static uint32    task_count = 0;

static bool      start_menu_open    = false;
static int32     hover_task_index   = -1;
static bool      hover_start_btn    = false;
static int32     hover_menu_item    = -1;
static int32     hover_menu_cat     = -1;
static bool      hover_shutdown     = false;
static bool      hover_restart      = false;

static char      clock_buf[16] = "00:00";

// Categories with all 43 apps
static Category categories[NUM_CATEGORIES] = {
    {
        "System Tools", 9,
        {
            { "NBench",    0, 1 },
            { "NeoMan",    1, 2 },
            { "Disk Tools",2, 3 },
            { "NeoTask",   3, 4 },
            { "NeoFind",   4, 5 },
            { "NeoZip",    5, 6 },
            { "NeoTheme",  6, 7 },
            { "NeoHelp",   7, 8 },
            { "NeoScript", 8, 9 }
        },
        true
    },
    {
        "Productivity", 4,
        {
            { "NeoWrite",  9,  10 },
            { "NeoCalc",   10, 11 },
            { "NeoPresent",11, 12 },
            { "NeoBase",   12, 13 }
        },
        false
    },
    {
        "Creative & Media", 7,
        {
            { "NeoEdit",    13, 14 },
            { "NeoPaint",   14, 15 },
            { "NeoTracker", 15, 16 },
            { "NeoAnim",    16, 17 },
            { "NeoCapture", 17, 18 },
            { "NeoView",    18, 19 },
            { "NeoPlayer",  19, 20 }
        },
        false
    },
    {
        "Network & Internet", 6,
        {
            { "NeoNet",  20, 21 },
            { "NeoBrowse",21, 22 },
            { "NeoMail", 22, 23 },
            { "NeoFTP",  23, 24 },
            { "NeoIRC",  24, 25 },
            { "NeoComm", 25, 26 }
        },
        false
    },
    {
        "Games", 5,
        {
            { "NeoMines", 26, 27 },
            { "NeoSol",   27, 28 },
            { "NeoTetris",28, 29 },
            { "NeoChess", 29, 30 },
            { "NeoSnake", 30, 31 }
        },
        false
    },
    {
        "Developer Tools", 4,
        {
            { "NeoASM",   31, 32 },
            { "NeoDebug", 32, 33 },
            { "NeoHex",   33, 34 },
            { "NeoGit",   34, 35 }
        },
        false
    },
    {
        "Desktop & System", 8,
        {
            { "NeoCalc2",       35, 36 },
            { "NeoClock",       36, 37 },
            { "NeoCalendar",    37, 38 },
            { "NeoAI",          38, 39 },
            { "File Manager",   39, 40 },
            { "System Monitor", 40, 41 },
            { "Settings",       41, 42 },
            { "NeoInstall",     42, 43 }
        },
        false
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32 str_len(const char* s) {
    uint32 n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static void str_copy(char* dst, const char* src, uint32 max_len) {
    uint32 i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void format_time(char* buf, uint32 seconds) {
    uint32 hours = (seconds / 3600) % 24;
    uint32 mins  = (seconds / 60) % 60;
    buf[0] = '0' + (char)(hours / 10);
    buf[1] = '0' + (char)(hours % 10);
    buf[2] = ':';
    buf[3] = '0' + (char)(mins / 10);
    buf[4] = '0' + (char)(mins % 10);
    buf[5] = '\0';
}

// ---------------------------------------------------------------------------
// Init / accessors
// ---------------------------------------------------------------------------

static void update_clock();  // forward declaration

void init() {
    for (uint32 i = 0; i < MAX_TASKS; i++) {
        tasks[i].window = nullptr;
        tasks[i].active = false;
    }
    task_count = 0;
    start_menu_open = false;
    hover_task_index = -1;
    hover_start_btn = false;
    hover_menu_item = -1;
    hover_menu_cat = -1;
    hover_shutdown = false;
    hover_restart = false;
    update_clock();
}

uint32 get_taskbar_height() {
    return TASKBAR_HEIGHT;
}

bool is_start_menu_open() {
    return start_menu_open;
}

void toggle_start_menu() {
    start_menu_open = !start_menu_open;
    hover_menu_item = -1;
    hover_menu_cat = -1;
    hover_shutdown = false;
    hover_restart = false;
}

// ---------------------------------------------------------------------------
// Task management
// ---------------------------------------------------------------------------

void add_task(neo::wm::Window* w) {
    if (!w) return;

    // Check if already present
    for (uint32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active && tasks[i].window == w) return;
    }

    // Find free slot
    for (uint32 i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) {
            tasks[i].window = w;
            tasks[i].active = true;
            task_count++;
            return;
        }
    }
}

void remove_task(neo::wm::Window* w) {
    for (uint32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active && tasks[i].window == w) {
            tasks[i].window = nullptr;
            tasks[i].active = false;
            if (task_count > 0) task_count--;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

void update_clock() {
    uint32 uptime = neo::timer::get_uptime_seconds();
    format_time(clock_buf, uptime);
}

// ---------------------------------------------------------------------------
// Drawing — Taskbar
// ---------------------------------------------------------------------------

static void draw_taskbar_background() {
    uint32 sw = gui::screen_width();
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);

    // Main background
    Rect bar_rect = { 0, bar_y, sw, TASKBAR_HEIGHT };
    gui::fill_rect(bar_rect, COL_BAR_BG);
    gui::alpha_blend(bar_rect, COL_GLASS);

    // Top border highlight (2px)
    Rect top_line = { 0, bar_y, sw, 1 };
    gui::fill_rect(top_line, COL_BAR_TOP_BORDER);
    Rect top_line2 = { 0, bar_y + 1, sw, 1 };
    gui::alpha_blend(top_line2, { 60, 90, 140, 80 });
}

static void draw_start_button() {
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);
    int32 btn_y = bar_y + ((int32)TASKBAR_HEIGHT - (int32)TASK_BTN_HEIGHT) / 2;

    Rect btn_rect = { 4, btn_y, START_BTN_WIDTH, TASK_BTN_HEIGHT };

    if (start_menu_open || hover_start_btn) {
        gui::fill_rect(btn_rect, COL_START_HOVER);
    } else {
        gui::fill_rect(btn_rect, COL_START_BG);
    }

    // Rounded corners
    gui::draw_rounded_rect(btn_rect, 3, { 100, 140, 200, 120 });

    // NeoBench logo icon
    gui::icon::draw_icon_color(8, btn_y + 3, 0, COL_TEXT_WHITE); // icon 0 = logo

    // "Start" text
    uint32 font = 1;
    uint32 tw = gui::text_width("Start", font);
    int32 tx = 4 + 28 + ((int32)START_BTN_WIDTH - 28 - (int32)tw) / 2;
    int32 ty = btn_y + ((int32)TASK_BTN_HEIGHT - (int32)gui::text_height(font)) / 2;
    gui::draw_text(tx, ty, "Start", COL_TEXT_WHITE, font);
}

static void draw_task_buttons() {
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);
    int32 btn_y = bar_y + ((int32)TASKBAR_HEIGHT - (int32)TASK_BTN_HEIGHT) / 2;
    int32 start_x = (int32)START_BTN_WIDTH + 12;

    uint32 font = 1;
    uint32 drawn = 0;

    for (uint32 i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active || !tasks[i].window) continue;

        neo::wm::Window* w = tasks[i].window;
        int32 bx = start_x + (int32)(drawn * (TASK_BTN_WIDTH + TASK_BTN_MARGIN));

        // Check if button fits before clock area
        uint32 sw = gui::screen_width();
        if (bx + (int32)TASK_BTN_WIDTH > (int32)(sw - CLOCK_WIDTH - TRAY_PADDING)) break;

        Rect btn_rect = { bx, btn_y, TASK_BTN_WIDTH, TASK_BTN_HEIGHT };

        // Color depends on state
        uint32 flags = neo::wm::get_flags(w);
        bool is_focused = false;
        // Access focused state through the window pointer
        // The window struct has focused as a public member
        // We check by comparing with the currently focused window
        neo::wm::Window* focused_win = neo::wm::get_focused();
        is_focused = (focused_win == w);

        if ((int32)i == hover_task_index) {
            gui::fill_rect(btn_rect, COL_TASK_HOVER);
        } else if (is_focused && (flags & WF_VISIBLE)) {
            gui::fill_rect(btn_rect, COL_TASK_FOCUSED);
        } else {
            gui::fill_rect(btn_rect, COL_TASK_BG);
        }

        gui::draw_rounded_rect(btn_rect, 2, { 80, 110, 160, 80 });

        // Draw truncated title
        // Access window title — it's a char[64] in the struct
        // We use gui::set_clip to truncate
        Rect text_clip = { bx + 6, btn_y, TASK_BTN_WIDTH - 12, TASK_BTN_HEIGHT };
        gui::set_clip(text_clip);

        int32 ty = btn_y + ((int32)TASK_BTN_HEIGHT - (int32)gui::text_height(font)) / 2;

        // Get title from the Window struct directly
        // Window.title is at offset after id (4 bytes)
        const char* title = (const char*)((uint8*)w + 4); // offset of title field
        gui::draw_text(bx + 6, ty, title, COL_TEXT_WHITE, font);

        gui::clear_clip();

        // Minimized indicator — subtle bottom bar
        if (flags & WF_MINIMIZED) {
            Rect ind = { bx + 4, btn_y + (int32)TASK_BTN_HEIGHT - 3, TASK_BTN_WIDTH - 8, 2 };
            gui::fill_rect(ind, COL_TEXT_GRAY);
        } else if (is_focused) {
            Rect ind = { bx + 4, btn_y + (int32)TASK_BTN_HEIGHT - 3, TASK_BTN_WIDTH - 8, 2 };
            gui::fill_rect(ind, { 100, 180, 255, 200 });
        }

        drawn++;
    }
}

static void draw_clock() {
    uint32 sw = gui::screen_width();
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);

    int32 cx = (int32)(sw - CLOCK_WIDTH - TRAY_PADDING);
    int32 cy = bar_y;

    // Subtle separator line
    gui::draw_line(cx, bar_y + 6, cx, bar_y + (int32)TASKBAR_HEIGHT - 6, { 80, 100, 130, 100 });

    // Clock text centered in tray area
    uint32 font = 1;
    uint32 tw = gui::text_width(clock_buf, font);
    uint32 th = gui::text_height(font);
    int32 tx = cx + ((int32)CLOCK_WIDTH - (int32)tw) / 2 + (int32)TRAY_PADDING / 2;
    int32 ty = bar_y + ((int32)TASKBAR_HEIGHT - (int32)th) / 2;
    gui::draw_text(tx, ty, clock_buf, COL_CLOCK_TEXT, font);
}

void draw() {
    draw_taskbar_background();
    draw_start_button();
    draw_task_buttons();
    draw_clock();
}

// ---------------------------------------------------------------------------
// Drawing — Start Menu
// ---------------------------------------------------------------------------

static uint32 calculate_menu_height() {
    uint32 height = MENU_PADDING * 2 + MENU_BOTTOM_HEIGHT;
    for (uint32 c = 0; c < NUM_CATEGORIES; c++) {
        height += MENU_HEADER_HEIGHT;
        if (categories[c].expanded) {
            height += categories[c].app_count * MENU_ITEM_HEIGHT;
        }
    }
    return height;
}

static void draw_menu_sidebar(Rect menu_rect) {
    Rect sidebar = {
        menu_rect.x,
        menu_rect.y,
        MENU_SIDEBAR_WIDTH,
        menu_rect.h
    };
    gui::fill_rect(sidebar, COL_MENU_SIDEBAR);

    // "NeoBench" vertical text / logo
    gui::icon::draw_icon_color(menu_rect.x + 10, menu_rect.y + 16, 0, { 100, 160, 220, 200 });

    // Vertical "NeoBench" label
    uint32 small_font = 0;
    int32 label_y = menu_rect.y + 52;
    const char* label = "NeoBench";
    for (uint32 i = 0; label[i] != '\0'; i++) {
        char ch[2] = { label[i], '\0' };
        int32 cx = menu_rect.x + ((int32)MENU_SIDEBAR_WIDTH - (int32)gui::text_width(ch, small_font)) / 2;
        gui::draw_text(cx, label_y, ch, { 80, 120, 170, 180 }, small_font);
        label_y += 14;
    }
}

static void draw_menu_items(Rect menu_rect) {
    int32 content_x = menu_rect.x + (int32)MENU_SIDEBAR_WIDTH + (int32)MENU_PADDING;
    int32 content_w = (int32)MENU_WIDTH - (int32)MENU_SIDEBAR_WIDTH - (int32)MENU_PADDING * 2;
    int32 y = menu_rect.y + (int32)MENU_PADDING;

    uint32 font_normal = 1;
    uint32 font_small = 0;
    int32 global_item = 0;

    for (uint32 c = 0; c < NUM_CATEGORIES; c++) {
        // Category header
        Rect header_rect = { content_x, y, (uint32)content_w, MENU_HEADER_HEIGHT };

        if ((int32)c == hover_menu_cat && hover_menu_item < 0) {
            gui::alpha_blend(header_rect, { 60, 80, 120, 80 });
        }

        // Expand/collapse arrow
        if (categories[c].expanded) {
            // Down arrow ▼
            int32 ax = content_x + 4;
            int32 ay = y + (int32)MENU_HEADER_HEIGHT / 2 - 2;
            gui::draw_line(ax, ay, ax + 8, ay, COL_MENU_HEADER);
            gui::draw_line(ax + 1, ay + 1, ax + 7, ay + 1, COL_MENU_HEADER);
            gui::draw_line(ax + 2, ay + 2, ax + 6, ay + 2, COL_MENU_HEADER);
            gui::draw_line(ax + 3, ay + 3, ax + 5, ay + 3, COL_MENU_HEADER);
        } else {
            // Right arrow ►
            int32 ax = content_x + 4;
            int32 ay = y + (int32)MENU_HEADER_HEIGHT / 2 - 4;
            gui::draw_line(ax, ay, ax, ay + 8, COL_MENU_HEADER);
            gui::draw_line(ax + 1, ay + 1, ax + 1, ay + 7, COL_MENU_HEADER);
            gui::draw_line(ax + 2, ay + 2, ax + 2, ay + 6, COL_MENU_HEADER);
            gui::draw_line(ax + 3, ay + 3, ax + 3, ay + 5, COL_MENU_HEADER);
        }

        // Category name (bold = larger font)
        int32 hty = y + ((int32)MENU_HEADER_HEIGHT - (int32)gui::text_height(font_normal)) / 2;
        gui::draw_text(content_x + 18, hty, categories[c].name, COL_MENU_HEADER, font_normal);

        y += (int32)MENU_HEADER_HEIGHT;

        // If expanded, draw app items
        if (categories[c].expanded) {
            for (uint32 a = 0; a < categories[c].app_count; a++) {
                Rect item_rect = { content_x, y, (uint32)content_w, MENU_ITEM_HEIGHT };

                // Hover highlight
                if ((int32)c == hover_menu_cat && (int32)a == hover_menu_item) {
                    gui::fill_rect(item_rect, COL_MENU_ITEM_HOVER);
                    gui::draw_rounded_rect(item_rect, 2, { 80, 120, 180, 60 });
                }

                // Icon
                uint32 icon_id = categories[c].apps[a].icon_id;
                int32 icon_y = y + ((int32)MENU_ITEM_HEIGHT - 16) / 2;
                gui::icon::draw_icon(content_x + 8, icon_y, icon_id);

                // App name text
                int32 text_y = y + ((int32)MENU_ITEM_HEIGHT - (int32)gui::text_height(font_small)) / 2;
                gui::draw_text(content_x + 30, text_y, categories[c].apps[a].name, COL_MENU_ITEM_TEXT, font_small);

                y += (int32)MENU_ITEM_HEIGHT;
                global_item++;
            }
        }
    }
}

static void draw_menu_bottom(Rect menu_rect) {
    int32 bottom_y = menu_rect.y + (int32)menu_rect.h - (int32)MENU_BOTTOM_HEIGHT;
    int32 content_x = menu_rect.x + (int32)MENU_SIDEBAR_WIDTH;
    uint32 content_w = MENU_WIDTH - MENU_SIDEBAR_WIDTH;

    // Separator line
    gui::draw_line(content_x + 8, bottom_y, content_x + (int32)content_w - 8, bottom_y,
                   { 80, 100, 130, 120 });

    uint32 font = 1;
    uint32 btn_w = (content_w - 24) / 2;
    uint32 btn_h = 28;
    int32 btn_y = bottom_y + ((int32)MENU_BOTTOM_HEIGHT - (int32)btn_h) / 2 + 2;

    // Shut Down button
    int32 sd_x = content_x + 8;
    Rect sd_rect = { sd_x, btn_y, btn_w, btn_h };
    if (hover_shutdown) {
        gui::fill_rect(sd_rect, COL_SHUTDOWN_HOVER);
    } else {
        gui::fill_rect(sd_rect, COL_SHUTDOWN_BG);
    }
    gui::draw_rounded_rect(sd_rect, 3, { 150, 60, 60, 120 });

    uint32 sd_tw = gui::text_width("Shut Down", font);
    int32 sd_tx = sd_x + ((int32)btn_w - (int32)sd_tw) / 2;
    int32 sd_ty = btn_y + ((int32)btn_h - (int32)gui::text_height(font)) / 2;
    gui::draw_text(sd_tx, sd_ty, "Shut Down", COL_TEXT_WHITE, font);

    // Restart button
    int32 rs_x = sd_x + (int32)btn_w + 8;
    Rect rs_rect = { rs_x, btn_y, btn_w, btn_h };
    if (hover_restart) {
        gui::alpha_blend(rs_rect, { 80, 100, 140, 180 });
    } else {
        gui::alpha_blend(rs_rect, { 50, 60, 80, 160 });
    }
    gui::draw_rounded_rect(rs_rect, 3, { 80, 110, 150, 100 });

    uint32 rs_tw = gui::text_width("Restart", font);
    int32 rs_tx = rs_x + ((int32)btn_w - (int32)rs_tw) / 2;
    int32 rs_ty = btn_y + ((int32)btn_h - (int32)gui::text_height(font)) / 2;
    gui::draw_text(rs_tx, rs_ty, "Restart", COL_TEXT_WHITE, font);
}

void draw_start_menu() {
    if (!start_menu_open) return;

    uint32 sh = gui::screen_height();
    uint32 menu_h = calculate_menu_height();

    // Clamp to screen
    uint32 max_h = sh - TASKBAR_HEIGHT - 10;
    if (menu_h > max_h) menu_h = max_h;

    int32 menu_x = 4;
    int32 menu_y = (int32)(sh - TASKBAR_HEIGHT) - (int32)menu_h;

    Rect menu_rect = { menu_x, menu_y, MENU_WIDTH, menu_h };

    // Glass background
    gui::fill_rect(menu_rect, COL_MENU_BG);
    gui::alpha_blend(menu_rect, COL_MENU_GLASS);

    // Border
    gui::draw_rect(menu_rect, COL_MENU_BORDER);

    // Sidebar
    draw_menu_sidebar(menu_rect);

    // Menu items
    draw_menu_items(menu_rect);

    // Bottom buttons
    draw_menu_bottom(menu_rect);
}

// ---------------------------------------------------------------------------
// Hover update
// ---------------------------------------------------------------------------

void update_hover(int32 mx, int32 my) {
    uint32 sw = gui::screen_width();
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);

    hover_start_btn = false;
    hover_task_index = -1;
    hover_shutdown = false;
    hover_restart = false;

    // Check start menu hover
    if (start_menu_open) {
        uint32 menu_h = calculate_menu_height();
        uint32 max_h = sh - TASKBAR_HEIGHT - 10;
        if (menu_h > max_h) menu_h = max_h;

        int32 menu_x = 4;
        int32 menu_y = (int32)(sh - TASKBAR_HEIGHT) - (int32)menu_h;

        if (mx >= menu_x && mx < menu_x + (int32)MENU_WIDTH &&
            my >= menu_y && my < menu_y + (int32)menu_h) {

            // Check bottom buttons
            int32 bottom_y = menu_y + (int32)menu_h - (int32)MENU_BOTTOM_HEIGHT;
            if (my >= bottom_y) {
                int32 content_x = menu_x + (int32)MENU_SIDEBAR_WIDTH;
                uint32 content_w = MENU_WIDTH - MENU_SIDEBAR_WIDTH;
                uint32 btn_w = (content_w - 24) / 2;
                uint32 btn_h = 28;
                int32 btn_y = bottom_y + ((int32)MENU_BOTTOM_HEIGHT - (int32)btn_h) / 2 + 2;

                if (my >= btn_y && my < btn_y + (int32)btn_h) {
                    if (mx >= content_x + 8 && mx < content_x + 8 + (int32)btn_w) {
                        hover_shutdown = true;
                    } else if (mx >= content_x + 8 + (int32)btn_w + 8 &&
                               mx < content_x + 8 + (int32)btn_w + 8 + (int32)btn_w) {
                        hover_restart = true;
                    }
                }
                hover_menu_cat = -1;
                hover_menu_item = -1;
                return;
            }

            // Check menu items
            int32 content_x = menu_x + (int32)MENU_SIDEBAR_WIDTH + (int32)MENU_PADDING;
            int32 y = menu_y + (int32)MENU_PADDING;
            hover_menu_cat = -1;
            hover_menu_item = -1;

            for (uint32 c = 0; c < NUM_CATEGORIES; c++) {
                // Category header
                if (my >= y && my < y + (int32)MENU_HEADER_HEIGHT) {
                    hover_menu_cat = (int32)c;
                    hover_menu_item = -1;
                    return;
                }
                y += (int32)MENU_HEADER_HEIGHT;

                if (categories[c].expanded) {
                    for (uint32 a = 0; a < categories[c].app_count; a++) {
                        if (my >= y && my < y + (int32)MENU_ITEM_HEIGHT) {
                            hover_menu_cat = (int32)c;
                            hover_menu_item = (int32)a;
                            return;
                        }
                        y += (int32)MENU_ITEM_HEIGHT;
                    }
                }
            }
            return;
        }
    }

    // Check taskbar area
    if (my < bar_y) return;

    // Start button
    int32 btn_y = bar_y + ((int32)TASKBAR_HEIGHT - (int32)TASK_BTN_HEIGHT) / 2;
    if (mx >= 4 && mx < 4 + (int32)START_BTN_WIDTH &&
        my >= btn_y && my < btn_y + (int32)TASK_BTN_HEIGHT) {
        hover_start_btn = true;
        return;
    }

    // Task buttons
    int32 start_x = (int32)START_BTN_WIDTH + 12;
    uint32 drawn = 0;
    for (uint32 i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) continue;

        int32 bx = start_x + (int32)(drawn * (TASK_BTN_WIDTH + TASK_BTN_MARGIN));
        if (bx + (int32)TASK_BTN_WIDTH > (int32)(sw - CLOCK_WIDTH - TRAY_PADDING)) break;

        if (mx >= bx && mx < bx + (int32)TASK_BTN_WIDTH &&
            my >= btn_y && my < btn_y + (int32)TASK_BTN_HEIGHT) {
            hover_task_index = (int32)i;
            return;
        }
        drawn++;
    }
}

// ---------------------------------------------------------------------------
// Click handling
// ---------------------------------------------------------------------------

int32 handle_click(int32 mx, int32 my) {
    uint32 sh = gui::screen_height();
    int32 bar_y = (int32)(sh - TASKBAR_HEIGHT);

    // Start menu click handling
    if (start_menu_open) {
        uint32 menu_h = calculate_menu_height();
        uint32 max_h = sh - TASKBAR_HEIGHT - 10;
        if (menu_h > max_h) menu_h = max_h;

        int32 menu_x = 4;
        int32 menu_y = (int32)(sh - TASKBAR_HEIGHT) - (int32)menu_h;

        if (mx >= menu_x && mx < menu_x + (int32)MENU_WIDTH &&
            my >= menu_y && my < menu_y + (int32)menu_h) {

            // Bottom buttons
            int32 bottom_y = menu_y + (int32)menu_h - (int32)MENU_BOTTOM_HEIGHT;
            if (my >= bottom_y) {
                if (hover_shutdown) return -2;  // shutdown signal
                if (hover_restart)  return -3;  // restart signal
                return -1;
            }

            // Category header click — toggle expand
            if (hover_menu_cat >= 0 && hover_menu_item < 0) {
                categories[hover_menu_cat].expanded = !categories[hover_menu_cat].expanded;
                return -1;
            }

            // App item click — return app_id
            if (hover_menu_cat >= 0 && hover_menu_item >= 0) {
                uint32 cat = (uint32)hover_menu_cat;
                uint32 app = (uint32)hover_menu_item;
                if (cat < NUM_CATEGORIES && app < categories[cat].app_count) {
                    int32 app_id = (int32)categories[cat].apps[app].app_id;
                    start_menu_open = false;
                    return app_id;
                }
            }

            return -1;
        }
    }

    // Taskbar area
    if (my < bar_y) {
        // Click outside taskbar and menu — close start menu
        if (start_menu_open) {
            start_menu_open = false;
        }
        return -1;
    }

    // Start button click
    int32 btn_y = bar_y + ((int32)TASKBAR_HEIGHT - (int32)TASK_BTN_HEIGHT) / 2;
    if (mx >= 4 && mx < 4 + (int32)START_BTN_WIDTH &&
        my >= btn_y && my < btn_y + (int32)TASK_BTN_HEIGHT) {
        toggle_start_menu();
        return -1;
    }

    // Close start menu if clicking elsewhere on taskbar
    if (start_menu_open) {
        start_menu_open = false;
    }

    // Task button click — focus / restore that window
    if (hover_task_index >= 0 && hover_task_index < (int32)MAX_TASKS) {
        TaskEntry* te = &tasks[hover_task_index];
        if (te->active && te->window) {
            uint32 flags = neo::wm::get_flags(te->window);
            if (flags & WF_MINIMIZED) {
                neo::wm::restore(te->window);
            }
            neo::wm::set_focused(te->window);
        }
        return -1;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Query — is point inside taskbar?
// ---------------------------------------------------------------------------

bool point_in_taskbar(int32 x, int32 y) {
    uint32 sh = gui::screen_height();
    return y >= (int32)(sh - TASKBAR_HEIGHT);
}

bool point_in_start_menu(int32 x, int32 y) {
    if (!start_menu_open) return false;

    uint32 sh = gui::screen_height();
    uint32 menu_h = calculate_menu_height();
    uint32 max_h = sh - TASKBAR_HEIGHT - 10;
    if (menu_h > max_h) menu_h = max_h;

    int32 menu_x = 4;
    int32 menu_y = (int32)(sh - TASKBAR_HEIGHT) - (int32)menu_h;

    return (x >= menu_x && x < menu_x + (int32)MENU_WIDTH &&
            y >= menu_y && y < menu_y + (int32)menu_h);
}

// ---------------------------------------------------------------------------
// Get app info for launching
// ---------------------------------------------------------------------------

const char* get_app_name(uint32 app_id) {
    for (uint32 c = 0; c < NUM_CATEGORIES; c++) {
        for (uint32 a = 0; a < categories[c].app_count; a++) {
            if (categories[c].apps[a].app_id == app_id) {
                return categories[c].apps[a].name;
            }
        }
    }
    return "Unknown";
}

uint32 get_app_icon(uint32 app_id) {
    for (uint32 c = 0; c < NUM_CATEGORIES; c++) {
        for (uint32 a = 0; a < categories[c].app_count; a++) {
            if (categories[c].apps[a].app_id == app_id) {
                return categories[c].apps[a].icon_id;
            }
        }
    }
    return 0;
}

} // namespace taskbar
} // namespace neo
