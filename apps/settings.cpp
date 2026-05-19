#include "../include/neobench.h"
#include "../lib/string.h"

// Settings - System Configuration Panels
// Display, Input, Sound, Memory, Network, Storage, System/About

namespace {

constexpr int NUM_PANELS = 7;

enum SettingsPanel {
    PAN_DISPLAY, PAN_INPUT, PAN_SOUND, PAN_MEMORY,
    PAN_NETWORK, PAN_STORAGE, PAN_SYSTEM
};

struct SettingsState {
    bool running;
    SettingsPanel panel;
    int panel_cursor;

    // Configurable values
    int kbd_repeat_rate;  // 1-10
    int kbd_repeat_delay; // 1-5
    int mouse_speed;      // 1-5
    int sound_volume;     // 0-100
    bool sound_enabled;
    char hostname[32];
    int hostname_len;
    bool editing_hostname;
};

static SettingsState cfg;

static const char* panel_names[] = {
    "Display", "Input", "Sound", "Memory", "Network", "Storage", "System"
};

static void draw_box(int x, int y, int w, int h) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');
    for (int r = 1; r < h - 1; r++) {
        neo::display::set_cursor(x, y + r);
        neo::display::putchar('|');
        neo::display::set_cursor(x + w - 1, y + r);
        neo::display::putchar('|');
    }
    neo::display::set_cursor(x, y + h - 1);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');
}

static void draw_slider(int x, int y, int w, int val, int max_val) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('[');
    int filled = (val * (w - 2)) / max_val;
    neo::display::set_color(10, 0);
    for (int i = 0; i < filled; i++) neo::display::putchar('=');
    neo::display::set_color(8, 0);
    for (int i = filled; i < w - 2; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);
    neo::display::putchar(']');
    char vbuf[8];
    ksprintf(vbuf, 8, " %d", val);
    neo::display::puts(vbuf);
}

static void draw_sidebar(int x, int y, int h) {
    for (int i = 0; i < NUM_PANELS; i++) {
        neo::display::set_cursor(x, y + i * 2);
        if ((int)cfg.panel == i) {
            neo::display::set_color(0, 14);
            neo::display::puts(" > ");
        } else {
            neo::display::set_color(7, 0);
            neo::display::puts("   ");
        }
        neo::display::puts(panel_names[i]);
        // Pad
        int len = neo_strlen(panel_names[i]);
        for (int p = len; p < 12; p++) neo::display::putchar(' ');
        neo::display::set_color(7, 0);
    }
}

static void draw_display_panel(int x, int y) {
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Display Settings");
    neo::display::set_bold(false);

    char buf[80];
    ksprintf(buf, 80, "Console Size:     %d x %d characters", w, h);
    neo::display::set_cursor(x, y + 2);
    neo::display::puts(buf);

    neo::display::set_cursor(x, y + 3);
    neo::display::puts("Display Type:     Text Mode Console (Bitplane)");

    neo::display::set_cursor(x, y + 4);
    neo::display::puts("Colors:           16 foreground / 8 background");

    neo::display::set_cursor(x, y + 6);
    neo::display::puts("Color Palette Preview:");

    for (int i = 0; i < 16; i++) {
        neo::display::set_cursor(x + (i % 8) * 6, y + 8 + (i / 8));
        neo::display::set_color(i, 0);
        char cbuf[8];
        ksprintf(cbuf, 8, " C%02d ", i);
        neo::display::puts(cbuf);
    }
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 11);
    neo::display::puts("Background Colors:");
    for (int i = 0; i < 8; i++) {
        neo::display::set_cursor(x + i * 6, y + 12);
        neo::display::set_color(15, i);
        char cbuf[8];
        ksprintf(cbuf, 8, " B%d  ", i);
        neo::display::puts(cbuf);
    }
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 14);
    neo::display::set_color(8, 0);
    neo::display::puts("(Display settings are read-only in this version)");
    neo::display::set_color(7, 0);
}

static void draw_input_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Input Settings");
    neo::display::set_bold(false);

    neo::display::set_cursor(x, y + 2);
    neo::display::puts("Keyboard:");

    neo::display::set_cursor(x + 2, y + 3);
    bool sel0 = (cfg.panel_cursor == 0);
    if (sel0) neo::display::set_color(14, 0);
    neo::display::puts("Repeat Rate:  ");
    draw_slider(x + 16, y + 3, 20, cfg.kbd_repeat_rate, 10);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x + 2, y + 4);
    bool sel1 = (cfg.panel_cursor == 1);
    if (sel1) neo::display::set_color(14, 0);
    neo::display::puts("Repeat Delay: ");
    draw_slider(x + 16, y + 4, 20, cfg.kbd_repeat_delay, 5);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 6);
    neo::display::puts("Mouse:");

    neo::display::set_cursor(x + 2, y + 7);
    bool sel2 = (cfg.panel_cursor == 2);
    if (sel2) neo::display::set_color(14, 0);
    neo::display::puts("Speed:        ");
    draw_slider(x + 16, y + 7, 20, cfg.mouse_speed, 5);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 9);
    neo::display::set_color(8, 0);
    neo::display::puts("Use Left/Right arrows to adjust values");
    neo::display::set_color(7, 0);
}

static void draw_sound_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Sound Settings");
    neo::display::set_bold(false);

    neo::display::set_cursor(x, y + 2);
    neo::display::puts("Audio Chip:    Paula (4 channels, 8-bit PCM)");

    neo::display::set_cursor(x + 2, y + 4);
    bool sel0 = (cfg.panel_cursor == 0);
    if (sel0) neo::display::set_color(14, 0);
    neo::display::puts("Master Volume: ");
    draw_slider(x + 17, y + 4, 25, cfg.sound_volume, 100);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x + 2, y + 6);
    bool sel1 = (cfg.panel_cursor == 1);
    if (sel1) neo::display::set_color(14, 0);
    neo::display::puts("Sound Enabled: ");
    neo::display::puts(cfg.sound_enabled ? "[ON] " : "[OFF]");
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x + 2, y + 8);
    neo::display::puts("Channel Status:");
    for (int ch = 0; ch < 4; ch++) {
        char cbuf[32];
        ksprintf(cbuf, 32, "  Channel %d: Ready", ch);
        neo::display::set_cursor(x + 4, y + 9 + ch);
        neo::display::set_color(10, 0);
        neo::display::puts(cbuf);
    }
    neo::display::set_color(7, 0);

    // Test sound button
    neo::display::set_cursor(x + 2, y + 14);
    bool sel2 = (cfg.panel_cursor == 2);
    if (sel2) neo::display::set_color(0, 14);
    neo::display::puts("  [ Test Sound ]  ");
    neo::display::set_color(7, 0);
}

static void draw_memory_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Memory Information");
    neo::display::set_bold(false);

    unsigned int total = neo::mem::get_total_mem();
    unsigned int free_mem = neo::mem::get_free_mem();
    unsigned int chip = neo::mem::get_free_chip();
    unsigned int fast = neo::mem::get_free_fast();

    char buf[80];
    ksprintf(buf, 80, "Total RAM:      %u bytes (%u KB)", total, total / 1024);
    neo::display::set_cursor(x, y + 2);
    neo::display::puts(buf);

    ksprintf(buf, 80, "Free RAM:       %u bytes (%u KB)", free_mem, free_mem / 1024);
    neo::display::set_cursor(x, y + 3);
    neo::display::puts(buf);

    ksprintf(buf, 80, "Used RAM:       %u bytes (%u KB)", total - free_mem, (total - free_mem) / 1024);
    neo::display::set_cursor(x, y + 4);
    neo::display::puts(buf);

    neo::display::set_cursor(x, y + 6);
    neo::display::puts("Chip RAM Free:  ");
    ksprintf(buf, 80, "%u bytes (%u KB)", chip, chip / 1024);
    neo::display::set_color(13, 0);
    neo::display::puts(buf);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 7);
    neo::display::puts("Fast RAM Free:  ");
    ksprintf(buf, 80, "%u bytes (%u KB)", fast, fast / 1024);
    neo::display::set_color(14, 0);
    neo::display::puts(buf);
    neo::display::set_color(7, 0);

    // Memory usage bar
    int pct = total > 0 ? (int)(((unsigned long long)(total - free_mem) * 100) / total) : 0;
    neo::display::set_cursor(x, y + 9);
    neo::display::puts("Usage: ");
    neo::display::putchar('[');
    int bar_w = 40;
    int filled = (pct * bar_w) / 100;
    neo::display::set_color(pct > 80 ? 12 : 10, 0);
    for (int i = 0; i < filled; i++) neo::display::putchar('#');
    neo::display::set_color(8, 0);
    for (int i = filled; i < bar_w; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);
    neo::display::putchar(']');
    ksprintf(buf, 80, " %d%%", pct);
    neo::display::puts(buf);
}

static void draw_network_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Network Settings");
    neo::display::set_bold(false);

    neo::display::set_cursor(x, y + 2);
    neo::display::puts("Probing Zorro bus for network cards...");

    // int cards = neo::network::probe_zorro();
    int cards = 0;
    char buf[80];
    ksprintf(buf, 80, "Network cards found: %d", cards);
    neo::display::set_cursor(x, y + 4);
    neo::display::puts(buf);

    if (cards > 0) {
        neo::display::set_cursor(x, y + 6);
        neo::display::set_color(10, 0);
        neo::display::puts("Network interface detected!");
        neo::display::set_color(7, 0);

        neo::display::set_cursor(x, y + 8);
        neo::display::puts("IP Address:    (not configured)");
        neo::display::set_cursor(x, y + 9);
        neo::display::puts("Subnet Mask:   255.255.255.0");
        neo::display::set_cursor(x, y + 10);
        neo::display::puts("Gateway:       (not configured)");
        neo::display::set_cursor(x, y + 11);
        neo::display::puts("DNS:           (not configured)");
    } else {
        neo::display::set_cursor(x, y + 6);
        neo::display::set_color(12, 0);
        neo::display::puts("No network hardware detected.");
        neo::display::set_color(7, 0);
        neo::display::set_cursor(x, y + 8);
        neo::display::puts("Install a Zorro Ethernet card to enable networking.");
    }
}

static void draw_storage_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("Storage Devices");
    neo::display::set_bold(false);

    // Mounts
    neo::filesystem::MountInfo mounts[8];
    int mc = neo::filesystem::list_mounts(mounts, 8);

    neo::display::set_color(11, 0);
    neo::display::set_cursor(x, y + 2);
    neo::display::puts("Mount Point      Type    Total      Free       Used");
    neo::display::set_color(8, 0);
    neo::display::set_cursor(x, y + 3);
    for (int i = 0; i < 55; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    for (int i = 0; i < mc; i++) {
        unsigned int total_kb = (unsigned long long)mounts[i].total_blocks * mounts[i].block_size / 1024;
        unsigned int free_kb = (unsigned long long)mounts[i].free_blocks * mounts[i].block_size / 1024;
        unsigned int used_kb = total_kb - free_kb;
        int pct = total_kb > 0 ? (int)(((unsigned long long)used_kb * 100) / total_kb) : 0;

        char line[80];
        ksprintf(line, 80, "%-16s %-7s %5uK     %5uK     %3d%%",
                 mounts[i].mount_point, mounts[i].fs_type, total_kb, free_kb, pct);
        neo::display::set_cursor(x, y + 4 + i);
        neo::display::puts(line);
    }

    // IDE/SCSI probing
    neo::display::set_cursor(x, y + 4 + mc + 1);
    neo::display::set_bold(true);
    neo::display::puts("IDE Devices:");
    neo::display::set_bold(false);

    neo::storage::DeviceInfo devs[4];
    int ide_count = neo::storage::ide::detect_drives(devs, 4);
    if (ide_count > 0) {
        for (int i = 0; i < ide_count; i++) {
            char line[80];
            ksprintf(line, 80, "  %s - %uMB (%s)", devs[i].model, devs[i].size_mb,
                     devs[i].is_master ? "Master" : "Slave");
            neo::display::set_cursor(x, y + 5 + mc + 1 + i);
            neo::display::puts(line);
        }
    } else {
        neo::display::set_cursor(x, y + 5 + mc + 1);
        neo::display::puts("  No IDE drives detected");
    }

    // SCSI
    int scsi_row = y + 6 + mc + 1 + (ide_count > 0 ? ide_count : 1);
    neo::display::set_cursor(x, scsi_row);
    neo::display::set_bold(true);
    neo::display::puts("SCSI Devices:");
    neo::display::set_bold(false);

    neo::storage::DeviceInfo sdevs[7];
    int scsi_count = neo::storage::scsi::detect_drives(sdevs, 7);
    if (scsi_count > 0) {
        for (int i = 0; i < scsi_count; i++) {
            char line[80];
            ksprintf(line, 80, "  ID%d: %s - %uMB", sdevs[i].scsi_id, sdevs[i].model, sdevs[i].size_mb);
            neo::display::set_cursor(x, scsi_row + 1 + i);
            neo::display::puts(line);
        }
    } else {
        neo::display::set_cursor(x, scsi_row + 1);
        neo::display::puts("  No SCSI drives detected");
    }
}

static void draw_system_panel(int x, int y) {
    neo::display::set_bold(true);
    neo::display::set_cursor(x, y);
    neo::display::puts("System Information");
    neo::display::set_bold(false);

    neo::display::set_cursor(x, y + 2);
    neo::display::puts("Hostname:    ");
    if (cfg.editing_hostname) {
        neo::display::set_color(14, 0);
        neo::display::puts(cfg.hostname);
        neo::display::putchar('_');
        neo::display::set_color(7, 0);
    } else {
        neo::display::puts(cfg.hostname);
        neo::display::set_color(8, 0);
        neo::display::puts("  (Enter to edit)");
        neo::display::set_color(7, 0);
    }

    neo::display::set_cursor(x, y + 4);
    neo::display::puts("OS:          NeoBench Kernel v1.0");
    neo::display::set_cursor(x, y + 5);
    neo::display::puts("Platform:    Amiga (M68K)");

    // CPU info
    neo::cpu::CpuInfo cpu;
    neo::cpu::detect(cpu);
    char buf[80];
    const char* cpu_names[] = {"68000","68010","68020","68030","68040","68060"};
    int ci = cpu.type;
    if (ci < 0 || ci > 5) ci = 0;
    ksprintf(buf, 80, "CPU:         Motorola %s @ %dMHz", cpu_names[ci], cpu.clock_mhz);
    neo::display::set_cursor(x, y + 6);
    neo::display::puts(buf);

    ksprintf(buf, 80, "FPU:         %s", cpu.fpu_type ? "Present" : "None");
    neo::display::set_cursor(x, y + 7);
    neo::display::puts(buf);

    ksprintf(buf, 80, "MMU:         %s", cpu.has_mmu ? "Present" : "None");
    neo::display::set_cursor(x, y + 8);
    neo::display::puts(buf);

    // Uptime
    unsigned int up = neo::timer::get_uptime_seconds();
    ksprintf(buf, 80, "Uptime:      %dd %dh %dm %ds", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);
    neo::display::set_cursor(x, y + 10);
    neo::display::puts(buf);

    // RTC
    if (neo::rtc::is_present()) {
        neo::rtc::DateTime dt;
        neo::rtc::read(dt);
        ksprintf(buf, 80, "Date/Time:   %d/%d/%d %02d:%02d:%02d", dt.month, dt.day, dt.year, dt.hour, dt.minute, dt.second);
        neo::display::set_cursor(x, y + 11);
        neo::display::puts(buf);
    }

    // About box
    neo::display::set_cursor(x, y + 13);
    neo::display::set_color(8, 0);
    for (int i = 0; i < 50; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    neo::display::set_cursor(x, y + 14);
    neo::display::set_color(14, 0);
    neo::display::puts("NeoBench - Bare Metal Amiga Benchmark Kernel");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(x, y + 15);
    neo::display::puts("A modern operating environment for classic Amiga hardware.");
    neo::display::set_cursor(x, y + 16);
    neo::display::puts("Supports 68000-68060, Chip/Fast RAM, IDE/SCSI, NBFS/FFS.");
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_color(15, 5);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("Settings");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    // Sidebar
    draw_sidebar(1, 2, h - 4);

    // Separator
    for (int r = 1; r < h - 1; r++) {
        neo::display::set_cursor(17, r);
        neo::display::set_color(8, 0);
        neo::display::putchar('|');
    }
    neo::display::set_color(7, 0);

    // Content
    int cx = 19, cy = 2;
    switch (cfg.panel) {
        case PAN_DISPLAY: draw_display_panel(cx, cy); break;
        case PAN_INPUT:   draw_input_panel(cx, cy); break;
        case PAN_SOUND:   draw_sound_panel(cx, cy); break;
        case PAN_MEMORY:  draw_memory_panel(cx, cy); break;
        case PAN_NETWORK: draw_network_panel(cx, cy); break;
        case PAN_STORAGE: draw_storage_panel(cx, cy); break;
        case PAN_SYSTEM:  draw_system_panel(cx, cy); break;
    }

    // Status bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("Up/Down=Panel  Left/Right=Adjust  Enter=Action  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());

    if (sc == 0x01) {
        if (cfg.editing_hostname) { cfg.editing_hostname = false; return; }
        cfg.running = false;
        return;
    }

    if (cfg.editing_hostname) {
        if (ch >= 32 && ch < 127 && cfg.hostname_len < 30) {
            cfg.hostname[cfg.hostname_len++] = ch;
            cfg.hostname[cfg.hostname_len] = 0;
        }
        if (sc == 0x41 && cfg.hostname_len > 0) {
            cfg.hostname[--cfg.hostname_len] = 0;
        }
        if (ch == '\r' || ch == '\n' || sc == 0x44) {
            cfg.editing_hostname = false;
        }
        return;
    }

    // Panel navigation
    if (sc == 0x4C) { // Up
        if ((int)cfg.panel > 0) { cfg.panel = (SettingsPanel)((int)cfg.panel - 1); cfg.panel_cursor = 0; }
    }
    if (sc == 0x4D) { // Down
        if ((int)cfg.panel < NUM_PANELS - 1) { cfg.panel = (SettingsPanel)((int)cfg.panel + 1); cfg.panel_cursor = 0; }
    }

    // Tab cycles through items in panel
    if (sc == 0x42) {
        int max_items = 1;
        if (cfg.panel == PAN_INPUT) max_items = 3;
        if (cfg.panel == PAN_SOUND) max_items = 3;
        cfg.panel_cursor = (cfg.panel_cursor + 1) % max_items;
    }

    // Left/Right adjust values
    if (cfg.panel == PAN_INPUT) {
        if (sc == 0x4E) { // Right
            if (cfg.panel_cursor == 0 && cfg.kbd_repeat_rate < 10) cfg.kbd_repeat_rate++;
            if (cfg.panel_cursor == 1 && cfg.kbd_repeat_delay < 5) cfg.kbd_repeat_delay++;
            if (cfg.panel_cursor == 2 && cfg.mouse_speed < 5) cfg.mouse_speed++;
        }
        if (sc == 0x4F) { // Left
            if (cfg.panel_cursor == 0 && cfg.kbd_repeat_rate > 1) cfg.kbd_repeat_rate--;
            if (cfg.panel_cursor == 1 && cfg.kbd_repeat_delay > 1) cfg.kbd_repeat_delay--;
            if (cfg.panel_cursor == 2 && cfg.mouse_speed > 1) cfg.mouse_speed--;
        }
    }

    if (cfg.panel == PAN_SOUND) {
        if (sc == 0x4E) { // Right
            if (cfg.panel_cursor == 0 && cfg.sound_volume < 100) cfg.sound_volume += 5;
            if (cfg.panel_cursor == 1) cfg.sound_enabled = !cfg.sound_enabled;
        }
        if (sc == 0x4F) { // Left
            if (cfg.panel_cursor == 0 && cfg.sound_volume > 0) cfg.sound_volume -= 5;
            if (cfg.panel_cursor == 1) cfg.sound_enabled = !cfg.sound_enabled;
        }
        // Enter on test sound
        if ((ch == '\r' || ch == '\n' || sc == 0x44) && cfg.panel_cursor == 2) {
            neo::audio::init();
            neo::audio::play_tone(0, 440, 200);
        }
    }

    if (cfg.panel == PAN_SYSTEM) {
        if (ch == '\r' || ch == '\n' || sc == 0x44) {
            cfg.editing_hostname = true;
        }
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&cfg, 0, sizeof(cfg));
    cfg.running = true;
    cfg.panel = PAN_DISPLAY;
    cfg.kbd_repeat_rate = 5;
    cfg.kbd_repeat_delay = 3;
    cfg.mouse_speed = 3;
    cfg.sound_volume = 75;
    cfg.sound_enabled = true;
    neo_strcpy(cfg.hostname, "amiga");
    cfg.hostname_len = 5;

    draw_ui();

    while (cfg.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_ui();
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("Settings exited.\n");
}
