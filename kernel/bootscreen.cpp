/*
 * NeoBench OS — Boot Screen
 * kernel/bootscreen.cpp
 *
 * Graphical boot sequence with 120+ Linux-style messages,
 * animated progress bar, and ASCII logo.
 * Runs on the GUI framebuffer (not text-mode).
 */

#include "gui.h"
#include "neobench.h"


namespace neo { namespace bootscreen {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const uint16 MAX_VISIBLE_LINES = 40;
static const uint16 MSG_DELAY_FRAMES  = 2;      // ~30 ms at 60 fps
static const uint16 PROGRESS_BAR_H    = 4;
static const uint16 STATUS_FOOTER_H   = 18;
static const uint16 LOGO_TOP_MARGIN   = 10;
static const uint16 MSG_LEFT_MARGIN   = 12;
static const uint16 MSG_LINE_HEIGHT   = 12;

// ---------------------------------------------------------------------------
// Status tags
// ---------------------------------------------------------------------------
enum StatusTag {
    TAG_OK   = 0,
    TAG_WARN = 1,
    TAG_FAIL = 2,
    TAG_INFO = 3
};

struct BootMessage {
    StatusTag   tag;
    const char* text;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool          s_verbose    = false;
static uint16        s_msg_index  = 0;
static uint16        s_scroll_top = 0;
static uint16        s_screen_w   = 0;
static uint16        s_screen_h   = 0;
static uint16        s_msg_area_y = 0;
static uint16        s_msg_area_h = 0;
static uint32        s_frame_ctr  = 0;

// ---------------------------------------------------------------------------
// ASCII logo (drawn as cyan text)
// ---------------------------------------------------------------------------
static const char* s_logo[] = {
    "  _   _            ____                  _     ",
    " | \\ | | ___  ___ | __ )  ___ _ __   ___| |__  ",
    " |  \\| |/ _ \\/ _ \\|  _ \\ / _ \\ '_ \\ / __| '_ \\ ",
    " | |\\  |  __/ (_) | |_) |  __/ | | | (__| | | |",
    " |_| \\_|\\___|\\___/|____/ \\___|_| |_|\\___|_| |_|",
    nullptr
};

static const char* VERSION_STR = "NeoBench OS v1.4.0  (build 20260330-m68k)";

// ---------------------------------------------------------------------------
// Boot message table — 125 messages across 9 phases
// ---------------------------------------------------------------------------
static const BootMessage s_messages[] = {
    // Phase 1 — Core Init (15)
    { TAG_INFO, "Starting NeoBench kernel v1.4.0 ..." },
    { TAG_OK,   "CPU: Motorola 68040 @ 25 MHz detected" },
    { TAG_OK,   "FPU: 68882 floating-point coprocessor online" },
    { TAG_OK,   "MMU: Translation tables configured (4 KB pages)" },
    { TAG_OK,   "Memory manager: initialised (8 MB Chip, 64 MB Fast)" },
    { TAG_OK,   "Interrupt controller: Amiga custom vectors installed" },
    { TAG_OK,   "Exception handlers registered for vectors 2-31" },
    { TAG_OK,   "Kernel heap: 512 KB reserved at 0x00F80000" },
    { TAG_OK,   "Stack guard pages enabled" },
    { TAG_OK,   "Boot parameters parsed (cmdline 128 bytes)" },
    { TAG_INFO, "Early console: framebuffer @ 0x00020000" },
    { TAG_OK,   "Clock: CIA-A TOD calibrated (50 Hz tick)" },
    { TAG_OK,   "Clock: CIA-B interval timer started" },
    { TAG_OK,   "Random seed: gathered INODE_SIZE bits from timer jitter" },
    { TAG_OK,   "Kernel log ring buffer allocated (16 KB)" },

    // Phase 2 — Hardware (20)
    { TAG_INFO, "Probing hardware ..." },
    { TAG_OK,   "Timer: CIA-A timer A configured (1 ms tick)" },
    { TAG_OK,   "Timer: CIA-B timer B configured (system profiler)" },
    { TAG_OK,   "Keyboard controller: 6570-036 reset, handshake OK" },
    { TAG_OK,   "Keyboard: scancode set 2, typematic 30 cps / 250 ms" },
    { TAG_OK,   "Mouse: port 0 two-button quadrature detected" },
    { TAG_OK,   "Mouse: default resolution 400 dpi" },
    { TAG_OK,   "Serial: 8520 UART0 at 0xBFD000 (115200 baud)" },
    { TAG_OK,   "Serial: 8520 UART1 at 0xBFD100 (disabled)" },
    { TAG_OK,   "Parallel: CIA-A port B active, accent mode off" },
    { TAG_OK,   "Chipset: Agnus 8375 rev 3 (2 MB Chip limit)" },
    { TAG_OK,   "Chipset: Denise 8373 — AGA support confirmed" },
    { TAG_OK,   "Chipset: Paula 8364 rev 4 — 4-ch DMA audio" },
    { TAG_INFO, "Display: ECS/AGA compatible mode available" },
    { TAG_OK,   "Display: RTG board scan — none found" },
    { TAG_OK,   "Gary: IDE interface present at 0xDA0000" },
    { TAG_OK,   "Gayle: detected rev 0x40" },
    { TAG_OK,   "RTC: Ricoh RP5C01 battery OK, date valid" },
    { TAG_OK,   "DMA: Blitter enabled, 2-cycle nasty mode" },
    { TAG_OK,   "DMA: Copper initialised, 1-wait VSYNC list" },

    // Phase 3 — Storage (15)
    { TAG_INFO, "Scanning storage subsystem ..." },
    { TAG_OK,   "IDE channel 0 master: CF 512 MB (SANDISK SDCFB-512)" },
    { TAG_OK,   "IDE channel 0 slave:  not present" },
    { TAG_OK,   "IDE channel 1 master: not present" },
    { TAG_WARN, "SCSI controller: not detected — skipping" },
    { TAG_OK,   "Block layer: registered /dev/hda (1000944 sectors)" },
    { TAG_OK,   "Partition: /dev/hda1  NBFS  480 MB" },
    { TAG_OK,   "Partition: /dev/hda2  swap   32 MB" },
    { TAG_OK,   "Swap: activating /dev/hda2 (32 MB) ... OK" },
    { TAG_OK,   "Disk cache: INODE_SIZE KB write-back, 64-entry hash" },
    { TAG_OK,   "Disk scheduler: elevator algorithm selected" },
    { TAG_OK,   "Floppy: DF0: 880 KB DD (no disk inserted)" },
    { TAG_WARN, "Floppy: DF1: not connected" },
    { TAG_OK,   "Removable media monitor: polling every 2 s" },
    { TAG_OK,   "Storage subsystem ready" },

    // Phase 4 — Filesystem (15)
    { TAG_INFO, "Mounting filesystems ..." },
    { TAG_OK,   "NBFS v2.1 driver loaded" },
    { TAG_OK,   "NBFS: checking journal on /dev/hda1 ..." },
    { TAG_OK,   "NBFS: journal clean, 0 orphan inodes" },
    { TAG_OK,   "NBFS: mounted /dev/hda1 on / (read-write)" },
    { TAG_OK,   "VFS: virtual filesystem switch initialised" },
    { TAG_OK,   "VFS: mounted devfs on /dev" },
    { TAG_OK,   "VFS: mounted procfs on /proc" },
    { TAG_OK,   "VFS: mounted sysfs on /sys" },
    { TAG_OK,   "VFS: mounted tmpfs on /tmp (4 MB)" },
    { TAG_OK,   "Inode cache: 1024 entries allocated" },
    { TAG_OK,   "Dentry cache: 512 entries allocated" },
    { TAG_OK,   "Path resolver: max depth 32, symlink limit 8" },
    { TAG_OK,   "File descriptor table: INODE_SIZE slots per process" },
    { TAG_OK,   "Filesystem layer ready" },

    // Phase 5 — Drivers (15)
    { TAG_INFO, "Loading device drivers ..." },
    { TAG_OK,   "Paula audio: 4 channels, 8-bit, 28 kHz max" },
    { TAG_OK,   "Audio mixer: master volume 80%, channel gain 1.0" },
    { TAG_OK,   "Display driver: PAL 640x512 interlaced available" },
    { TAG_OK,   "Display driver: NTSC 640x400 available" },
    { TAG_OK,   "Display driver: AGA INODE_SIZE-color mode enabled" },
    { TAG_OK,   "Framebuffer: /dev/fb0 mapped at 0x00020000" },
    { TAG_OK,   "Zorro II bus: scanning slots 0-4 ..." },
    { TAG_WARN, "Zorro II bus: no expansion boards found" },
    { TAG_OK,   "Network: SLIP/PPP serial stack compiled in" },
    { TAG_WARN, "Network: no Ethernet adapter detected" },
    { TAG_OK,   "Loopback: lo0 127.0.0.1 UP" },
    { TAG_OK,   "Printer driver: parallel (LPT) registered" },
    { TAG_OK,   "Input: /dev/input/mouse0 ready" },
    { TAG_OK,   "Input: /dev/input/kbd0 ready" },

    // Phase 6 — Services (15)
    { TAG_INFO, "Starting system services ..." },
    { TAG_OK,   "Process manager: PID 1 (init) running" },
    { TAG_OK,   "Scheduler: round-robin, 20 ms quantum" },
    { TAG_OK,   "Scheduler: 4 priority levels configured" },
    { TAG_OK,   "IPC: message ports allocated (64 ports)" },
    { TAG_OK,   "IPC: shared memory segments enabled" },
    { TAG_OK,   "Signal subsystem: 16 user signals available" },
    { TAG_OK,   "Security module: user/root separation active" },
    { TAG_OK,   "Clipboard: shared buffer 8 KB allocated" },
    { TAG_OK,   "Environment: PATH=/bin:/usr/bin:/sbin" },
    { TAG_OK,   "syslog daemon: started, log to /var/log/messages" },
    { TAG_OK,   "cron daemon: schedule table loaded (0 entries)" },
    { TAG_OK,   "Hostname set: neobench" },
    { TAG_OK,   "DNS resolver: nameserver 0.0.0.0 (loopback only)" },
    { TAG_OK,   "Services layer ready" },

    // Phase 7 — Desktop environment (15)
    { TAG_INFO, "Initialising desktop environment ..." },
    { TAG_OK,   "GUI framebuffer: acquired 640x512x8 surface" },
    { TAG_OK,   "Font engine: loaded built-in bitmap fonts (3 faces)" },
    { TAG_OK,   "Font cache: pre-rasterised ASCII 32-126" },
    { TAG_OK,   "Icon set: 48 system icons loaded" },
    { TAG_OK,   "Window manager: initialised (max 32 windows)" },
    { TAG_OK,   "Window decorator: title bar 24 px, border 2 px" },
    { TAG_OK,   "Theme engine: 'NeoBench Default' loaded" },
    { TAG_OK,   "Wallpaper engine: 6 procedural generators ready" },
    { TAG_OK,   "Cursor: 16x16 sprite, 2-color hardware pointer" },
    { TAG_OK,   "Taskbar: 40 px, clock + start menu + task buttons" },
    { TAG_OK,   "Drag-and-drop subsystem: initialised" },
    { TAG_OK,   "Tooltip manager: delay 500 ms" },
    { TAG_OK,   "Double-click interval: 400 ms" },
    { TAG_OK,   "Desktop environment ready" },

    // Phase 8 — Applications (15)
    { TAG_INFO, "Loading application registry ..." },
    { TAG_OK,   "Scanning /usr/share/apps/ for .npk packages ..." },
    { TAG_OK,   "Registered: NeoEdit (text editor) v2.1" },
    { TAG_OK,   "Registered: NeoBrowse (web browser) v1.0" },
    { TAG_OK,   "Registered: NeoCalc (calculator) v1.5" },
    { TAG_OK,   "Registered: NeoTerm (terminal) v3.0" },
    { TAG_OK,   "Registered: NeoPaint (image editor) v1.2" },
    { TAG_OK,   "Registered: File Manager v2.0" },
    { TAG_OK,   "Registered: System Monitor v1.1" },
    { TAG_OK,   "Registered: Settings v1.0" },
    { TAG_OK,   "Registered: Clock v1.0" },
    { TAG_OK,   "Registered: Music Player v0.9" },
    { TAG_OK,   "Registered: Help Viewer v1.0" },
    { TAG_INFO, "43 applications registered from 12 packages" },
    { TAG_OK,   "Application registry loaded" },

    // Phase 9 — Final (5)
    { TAG_INFO, "Finalising boot sequence ..." },
    { TAG_OK,   "System health check: all subsystems nominal" },
    { TAG_OK,   "Boot time: 3.72 seconds (125 init steps)" },
    { TAG_OK,   "NeoBench OS is ready." },
    { TAG_INFO, "Starting desktop environment ..." },
};

static const uint16 NUM_MESSAGES = sizeof(s_messages) / sizeof(s_messages[0]);

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------
static gui::Color col_black()  { return gui::Color{0, 0, 0, 255}; }
static gui::Color col_cyan()   { return gui::Color{0, 210, 230, 255}; }
static gui::Color col_green()  { return gui::Color{0, 220, 64, 255}; }
static gui::Color col_yellow() { return gui::Color{220, 200, 0, 255}; }
static gui::Color col_red()    { return gui::Color{220, 40, 40, 255}; }
static gui::Color col_gray()   { return gui::Color{140, 140, 140, 255}; }
static gui::Color col_dkgray() { return gui::Color{40, 40, 40, 255}; }
static gui::Color col_white()  { return gui::Color{220, 220, 220, 255}; }
static gui::Color col_bar_bg() { return gui::Color{30, 30, 30, 255}; }

// ---------------------------------------------------------------------------
// Tag string + colour
// ---------------------------------------------------------------------------
static const char* tag_string(StatusTag t) {
    switch (t) {
        case TAG_OK:   return "[  ok  ]";
        case TAG_WARN: return "[ warn ]";
        case TAG_FAIL: return "[ fail ]";
        case TAG_INFO: return "[ info ]";
    }
    return "[  ??  ]";
}

static gui::Color tag_color(StatusTag t) {
    switch (t) {
        case TAG_OK:   return col_green();
        case TAG_WARN: return gui::Color{255, 191, 0, 255}; // Amber
        case TAG_FAIL: return col_red();
        case TAG_INFO: return col_cyan();
    }
    return col_white();
}

// ---------------------------------------------------------------------------
// draw_logo — render ASCII art at top of screen
// ---------------------------------------------------------------------------
static void draw_logo() {
    uint16 y = LOGO_TOP_MARGIN;
    for (uint16 i = 0; s_logo[i] != nullptr; ++i) {
        int tw = gui::text_width(s_logo[i], gui::FontSize::Small);
        int x  = (s_screen_w - tw) / 2;
        if (x < 0) x = 4;
        gui::draw_text(x, y, s_logo[i], col_cyan(), gui::FontSize::Small);
        y += gui::text_height(gui::FontSize::Small) + 1;
    }
    // version
    int tw = gui::text_width(VERSION_STR, gui::FontSize::Small);
    int vx = (s_screen_w - tw) / 2;
    if (vx < 0) vx = 4;
    gui::draw_text(vx, y + 4, VERSION_STR, col_gray(), gui::FontSize::Small);
}

// ---------------------------------------------------------------------------
// draw_progress — draw progress bar near bottom
// ---------------------------------------------------------------------------
static void draw_progress(uint16 percent) {
    if (percent > 100) percent = 100;
    uint16 bar_y = s_screen_h - STATUS_FOOTER_H - PROGRESS_BAR_H - 6;
    gui::Rect track = { 0, (int32)bar_y, (int32)s_screen_w, PROGRESS_BAR_H };
    gui::fill_rect(track, col_bar_bg());

    uint32 fill_w = ((uint32)s_screen_w * percent) / 100;
    if (fill_w > 0) {
        gui::Rect fill = { 0, (int32)bar_y, (int32)fill_w, PROGRESS_BAR_H };
        gui::fill_rect(fill, col_green());
    }
}

// ---------------------------------------------------------------------------
// draw_footer — status hint at very bottom
// ---------------------------------------------------------------------------
static void draw_footer() {
    const char* hint = "Press ESC for verbose mode  |  F1 for recovery shell";
    int tw = gui::text_width(hint, gui::FontSize::Small);
    int x  = (s_screen_w - tw) / 2;
    int y  = s_screen_h - STATUS_FOOTER_H + 2;
    gui::fill_rect(gui::Rect{0, y - 2, (int32)s_screen_w, STATUS_FOOTER_H}, col_black());
    gui::draw_text(x, y, hint, col_gray(), gui::FontSize::Small);
}

// ---------------------------------------------------------------------------
// draw_message — render a single boot line
// ---------------------------------------------------------------------------
static void draw_message(uint16 line_index, const BootMessage& msg) {
    uint16 max_lines = s_msg_area_h / MSG_LINE_HEIGHT;
    uint16 visible_index;

    if (line_index >= s_scroll_top + max_lines) {
        s_scroll_top = line_index - max_lines + 1;
    }
    visible_index = line_index - s_scroll_top;

    int y = s_msg_area_y + visible_index * MSG_LINE_HEIGHT;
    int x = MSG_LEFT_MARGIN;

    // Clear line
    gui::fill_rect(gui::Rect{0, y, (int32)s_screen_w, MSG_LINE_HEIGHT}, col_black());

    // Tag
    const char* tag = tag_string(msg.tag);
    gui::draw_text(x, y, tag, tag_color(msg.tag), gui::FontSize::Small);
    x += gui::text_width(tag, gui::FontSize::Small) + 8;

    // Message text
    gui::draw_text(x, y, msg.text, col_white(), gui::FontSize::Small);
}

// ---------------------------------------------------------------------------
// draw_all_visible — redraw entire visible message area (after scroll)
// ---------------------------------------------------------------------------
static void draw_all_visible() {
    uint16 max_lines = s_msg_area_h / MSG_LINE_HEIGHT;
    gui::fill_rect(gui::Rect{0, (int32)s_msg_area_y,
                   (int32)s_screen_w, (int32)s_msg_area_h}, col_black());

    uint16 end = s_msg_index;
    uint16 start = s_scroll_top;
    for (uint16 i = start; i < end && (i - start) < max_lines; ++i) {
        int y = s_msg_area_y + (i - start) * MSG_LINE_HEIGHT;
        int x = MSG_LEFT_MARGIN;
        const BootMessage& msg = s_messages[i];
        gui::draw_text(x, y, tag_string(msg.tag), tag_color(msg.tag), gui::FontSize::Small);
        x += gui::text_width(tag_string(msg.tag), gui::FontSize::Small) + 8;
        gui::draw_text(x, y, msg.text, col_white(), gui::FontSize::Small);
    }
}

// ---------------------------------------------------------------------------
// check_keyboard — poll for ESC / F1
// ---------------------------------------------------------------------------
static void check_keyboard() {
    if (!neo::keyboard::key_available()) return;
    uint8 scancode = neo::keyboard::read_scancode();
    if (scancode == 0x45) {  // ESC pressed
        s_verbose = !s_verbose;
    }
    // F1 = 0x50 — recovery shell not implemented in boot screen
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void init() {
    s_screen_w   = gui::screen_width();
    s_screen_h   = gui::screen_height();
    s_msg_index  = 0;
    s_scroll_top = 0;
    s_frame_ctr  = 0;
    s_verbose    = false;

    // Calculate message area: below logo, above progress bar
    // Logo is ~6 lines of small font + margin
    uint16 logo_h = (gui::text_height(gui::FontSize::Small) + 1) * 6 + LOGO_TOP_MARGIN + 20;
    s_msg_area_y  = logo_h;
    uint16 bottom_reserved = STATUS_FOOTER_H + PROGRESS_BAR_H + 12;
    s_msg_area_h  = s_screen_h - s_msg_area_y - bottom_reserved;
}

// ---------------------------------------------------------------------------
// draw_memory_test — brief animated memory test display above messages
// ---------------------------------------------------------------------------
static void draw_memory_test() {
    // Display a quick memory test animation (8 steps)
    const char* mem_phases[] = {
        "Memory test: Chip RAM ... ",
        "Memory test: Chip RAM 2 MB OK",
        "Memory test: Fast RAM bank 0 ... ",
        "Memory test: Fast RAM bank 0 — 16 MB OK",
        "Memory test: Fast RAM bank 1 ... ",
        "Memory test: Fast RAM bank 1 — 16 MB OK",
        "Memory test: Fast RAM bank 2 ... ",
        "Memory test: Fast RAM bank 2 — 16 MB OK",
        "Memory test: Fast RAM bank 3 ... ",
        "Memory test: Fast RAM bank 3 — 16 MB OK",
        "Memory test: Total 66 MB available",
    };
    static const uint16 NUM_MEM_PHASES = 11;

    int32 mem_y = s_msg_area_y - MSG_LINE_HEIGHT - 4;
    gui::Color mem_col = gui::Color{100, 180, 255, 255};

    for (uint16 mp = 0; mp < NUM_MEM_PHASES; ++mp) {
        gui::begin_frame();

        // Clear the memory test line
        gui::fill_rect(gui::Rect{0, mem_y, (int32)s_screen_w, MSG_LINE_HEIGHT + 2},
                        col_black());

        // Draw current phase text
        gui::draw_text(MSG_LEFT_MARGIN, mem_y, mem_phases[mp], mem_col, gui::FontSize::Small);

        // Small progress indicator for each phase
        uint16 pct = (uint16)(((uint32)(mp + 1) * 100) / NUM_MEM_PHASES);
        int32 ind_x = s_screen_w - 80;
        char pct_buf[8];
        ksprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
        gui::draw_text(ind_x, mem_y, pct_buf, col_green(), gui::FontSize::Small);

        gui::end_frame();

        // Brief delay per phase (3 frames ≈ 50ms)
        for (uint16 d = 0; d < 3; ++d) {
            gui::begin_frame();
            gui::end_frame();
        }
    }

    // Clear memory test line before messages start
    gui::fill_rect(gui::Rect{0, mem_y, (int32)s_screen_w, MSG_LINE_HEIGHT + 2},
                    col_black());
}

// ---------------------------------------------------------------------------
// draw_spinner — animated spinner character for "working" indication
// ---------------------------------------------------------------------------
static char spinner_char() {
    static const char spin[] = { '|', '/', '-', '\\' };
    return spin[(s_frame_ctr / 4) & 3];
}

// ---------------------------------------------------------------------------
// draw_phase_header — draw a phase separator/header line
// ---------------------------------------------------------------------------
static void draw_phase_header(const char* phase_name) {
    // Draw a dimmed separator line with the phase name
    uint16 max_lines = s_msg_area_h / MSG_LINE_HEIGHT;
    if (s_msg_index >= s_scroll_top + max_lines) {
        s_scroll_top = s_msg_index - max_lines + 1;
    }
    uint16 vis = s_msg_index - s_scroll_top;
    int32 y = s_msg_area_y + vis * MSG_LINE_HEIGHT;
    int32 x = MSG_LEFT_MARGIN;

    gui::fill_rect(gui::Rect{0, y, (int32)s_screen_w, MSG_LINE_HEIGHT}, col_black());

    // Dashed line
    gui::Color dim = gui::Color{50, 50, 60, 255};
    gui::fill_rect(gui::Rect{x, y + MSG_LINE_HEIGHT / 2, s_screen_w - x * 2, 1}, dim);

    // Phase name centered
    int pw = gui::text_width(phase_name, gui::FontSize::Small);
    int px = (s_screen_w - pw) / 2;
    // Background behind text
    gui::fill_rect(gui::Rect{px - 4, y, pw + 8, MSG_LINE_HEIGHT}, col_black());
    gui::draw_text(px, y, phase_name, gui::Color{80, 90, 120, 255}, gui::FontSize::Small);
}

// ---------------------------------------------------------------------------
// draw_completion_banner — shown when all messages are done
// ---------------------------------------------------------------------------
static void draw_completion_banner() {
    int32 banner_y = s_screen_h / 2 - 20;
    int32 banner_h = 40;
    gui::Rect banner = { 0, banner_y, (int32)s_screen_w, banner_h };

    // Subtle green tinted bar
    gui::alpha_blend(banner, gui::Color{0, 60, 20, 120});

    const char* msg = "NeoBench OS is ready — Starting desktop environment...";
    int tw = gui::text_width(msg, gui::FontSize::Normal);
    int tx = (s_screen_w - tw) / 2;
    int ty = banner_y + (banner_h - gui::text_height(gui::FontSize::Normal)) / 2;
    gui::draw_text(tx, ty, msg, col_green(), gui::FontSize::Normal);
}

// ---------------------------------------------------------------------------
// fade_out — gradual fade to black before desktop starts
// ---------------------------------------------------------------------------
static void fade_out() {
    for (uint16 alpha = 0; alpha <= 255; alpha += 15) {
        gui::begin_frame();
        // Overlay increasingly opaque black rect
        gui::alpha_blend(
            gui::Rect{0, 0, (int32)s_screen_w, (int32)s_screen_h},
            gui::Color{0, 0, 0, (uint8)alpha}
        );
        gui::end_frame();
    }
    // Final solid black frame
    gui::begin_frame();
    gui::fill_rect(gui::Rect{0, 0, (int32)s_screen_w, (int32)s_screen_h}, col_black());
    gui::end_frame();
}

// ---------------------------------------------------------------------------
// Phase boundary indices (for drawing phase headers in verbose mode)
// ---------------------------------------------------------------------------
static const uint16 PHASE_BOUNDARIES[] = {
    0,    // Phase 1 - Core Init
    15,   // Phase 2 - Hardware
    35,   // Phase 3 - Storage
    50,   // Phase 4 - Filesystem
    65,   // Phase 5 - Drivers
    80,   // Phase 6 - Services
    95,   // Phase 7 - Desktop
    110,  // Phase 8 - Applications
    125,  // Phase 9 - Final
};
static const char* PHASE_NAMES[] = {
    "[ Core Initialisation ]",
    "[ Hardware Detection ]",
    "[ Storage Subsystem ]",
    "[ Filesystem Layer ]",
    "[ Device Drivers ]",
    "[ System Services ]",
    "[ Desktop Environment ]",
    "[ Application Registry ]",
    "[ Final Checks ]",
};
static const uint16 NUM_PHASES = 9;

static bool is_phase_boundary(uint16 idx) {
    for (uint16 p = 0; p < NUM_PHASES; ++p) {
        if (PHASE_BOUNDARIES[p] == idx) return true;
    }
    return false;
}

static const char* phase_name_for(uint16 idx) {
    for (uint16 p = 0; p < NUM_PHASES; ++p) {
        if (PHASE_BOUNDARIES[p] == idx) return PHASE_NAMES[p];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// log — add a single boot message dynamically
// ---------------------------------------------------------------------------
void log(StatusTag tag, const char* msg) {
    if (s_msg_index >= NUM_MESSAGES) return;

    // In a real scenario, we might want to store dynamic messages
    // but for now we'll just use the existing logic to draw
    
    gui::begin_frame();
    
    // If it's a new phase, draw header (if in verbose mode)
    if (s_verbose && is_phase_boundary(s_msg_index)) {
        const char* pname = phase_name_for(s_msg_index);
        if (pname) draw_phase_header(pname);
    }

    draw_message(s_msg_index, {tag, msg});
    s_msg_index++;

    // Update progress
    uint16 percent = (uint16)(((uint32)s_msg_index * 100) / NUM_MESSAGES);
    draw_progress(percent);

    gui::end_frame();
}

// ---------------------------------------------------------------------------
// finish — completion sequence
// ---------------------------------------------------------------------------
void finish() {
    // Completion: hold with banner for ~1 second (60 frames)
    for (uint16 i = 0; i < 60; ++i) {
        gui::begin_frame();
        draw_progress(100);
        if (i >= 10) {
            draw_completion_banner();
        }
        // Clear spinner
        int32 spin_x = s_screen_w - 24;
        int32 spin_y = s_screen_h - STATUS_FOOTER_H + 2;
        gui::fill_rect(gui::Rect{spin_x - 2, spin_y, 16, 14}, col_black());
        gui::draw_text(spin_x, spin_y, "*", col_green(), gui::FontSize::Small);
        gui::end_frame();
    }

    // Fade out to black
    fade_out();
}

// ---------------------------------------------------------------------------
// run — main boot animation loop (DEPRECATED: use log() instead)
// ---------------------------------------------------------------------------
void run() {
    init();

    // Phase 0: Draw static elements
    gui::begin_frame();
    gui::fill_rect(gui::Rect{0, 0, (int32)s_screen_w, (int32)s_screen_h}, col_black());
    draw_logo();
    draw_footer();
    draw_progress(0);
    gui::end_frame();

    // Phase 0.5: Memory test animation
    draw_memory_test();

    // The rest is now driven by kernel.cpp calling log()
}

// ---------------------------------------------------------------------------
// is_verbose_mode
// ---------------------------------------------------------------------------
bool is_verbose_mode() {
    return s_verbose;
}

}} // namespace neo::bootscreen
