#include "../include/neobench.h"
#include "../lib/string.h"

// NeoInstall - Hard Drive Installer
// Steps: Welcome, License, Partition Editor, Install Type, Components,
// Copy files with progress, Boot sector, Complete

namespace {

constexpr int MAX_COMPONENTS = 43;
constexpr int STEP_WELCOME = 0;
constexpr int STEP_LICENSE = 1;
constexpr int STEP_PARTITION = 2;
constexpr int STEP_TYPE = 3;
constexpr int STEP_COMPONENTS = 4;
constexpr int STEP_INSTALL = 5;
constexpr int STEP_BOOTBLOCK = 6;
constexpr int STEP_COMPLETE = 7;
constexpr int NUM_STEPS = 8;

struct Component {
    const char* name;
    const char* category;
    int size_kb;
    bool selected;
    bool required;
};

struct DriveInfo {
    char model[41];
    unsigned int size_mb;
    bool is_master;
    bool has_partition;
    unsigned int part_start;
    unsigned int part_size;
    bool formatted;
};

enum InstallType { INST_FULL, INST_MINIMAL, INST_CUSTOM };

struct InstallerState {
    bool running;
    int step;
    int cursor;
    int scroll;

    // License
    bool license_accepted;
    int license_scroll;

    // Drives
    DriveInfo drives[4];
    int drive_count;
    int selected_drive;

    // Partition
    unsigned int part_start_mb;
    unsigned int part_size_mb;
    bool part_created;

    // Install type
    InstallType install_type;

    // Components
    Component components[MAX_COMPONENTS];
    int comp_count;

    // Install progress
    int install_progress; // 0-100
    int current_file;
    int total_files;
    char current_filename[64];
    bool install_complete;
    bool boot_written;
};

static InstallerState inst;

static void init_components() {
    struct CompDef { const char* name; const char* cat; int size; bool req; };
    static const CompDef defs[] = {
        {"kernel",        "System",  64,  true},
        {"shell",         "System",  32,  true},
        {"init",          "System",  16,  true},
        {"drivers",       "System",  48,  true},
        {"neoedit",       "Editors", 24,  false},
        {"neoedit2",      "Editors", 32,  false},
        {"hexedit",       "Editors", 20,  false},
        {"neopaint",      "Graphics",28,  false},
        {"neodraw",       "Graphics",36,  false},
        {"imageview",     "Graphics",24,  false},
        {"neocalc",       "Desktop", 16,  false},
        {"neocalc2",      "Desktop", 28,  false},
        {"neoclock",      "Desktop", 20,  false},
        {"neocalendar",   "Desktop", 32,  false},
        {"neoai",         "Desktop", 40,  false},
        {"filemanager",   "Desktop", 36,  false},
        {"sysmonitor",    "Desktop", 28,  false},
        {"settings",      "Desktop", 24,  false},
        {"snake",         "Games",   16,  false},
        {"tetris",        "Games",   20,  false},
        {"chess",         "Games",   48,  false},
        {"minesweeper",   "Games",   16,  false},
        {"breakout",      "Games",   18,  false},
        {"adventure",     "Games",   36,  false},
        {"neosynth",      "Audio",   32,  false},
        {"audioplayer",   "Audio",   24,  false},
        {"benchmark",     "Tools",   20,  false},
        {"disktest",      "Tools",   16,  false},
        {"memtest",       "Tools",   12,  false},
        {"nettools",      "Tools",   20,  false},
        {"terminal",      "Tools",   16,  false},
        {"archiver",      "Tools",   24,  false},
        {"neobasic",      "Dev",     40,  false},
        {"assembler",     "Dev",     36,  false},
        {"debugger",      "Dev",     32,  false},
        {"neolisp",       "Dev",     28,  false},
        {"forth",         "Dev",     24,  false},
        {"pong",          "Games",   12,  false},
        {"tictactoe",     "Games",   10,  false},
        {"pipes",         "Games",   16,  false},
        {"neoinstall",    "System",  32,  true},
        {"fonts",         "System",  48,  false},
        {"docs",          "System",  64,  false},
    };

    inst.comp_count = MAX_COMPONENTS;
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        inst.components[i].name = defs[i].name;
        inst.components[i].category = defs[i].cat;
        inst.components[i].size_kb = defs[i].size;
        inst.components[i].required = defs[i].req;
        inst.components[i].selected = defs[i].req; // required ones pre-selected
    }
}

static void detect_drives() {
    neo::storage::DeviceInfo devs[4];
    inst.drive_count = neo::storage::ide::detect_drives(devs, 4);
    for (int i = 0; i < inst.drive_count; i++) {
        neo_strcpy(inst.drives[i].model, devs[i].model);
        inst.drives[i].size_mb = devs[i].size_mb;
        inst.drives[i].is_master = devs[i].is_master;
        inst.drives[i].has_partition = false;
        inst.drives[i].formatted = false;
    }
    if (inst.drive_count == 0) {
        // Add a placeholder
        neo_strcpy(inst.drives[0].model, "Virtual HD");
        inst.drives[0].size_mb = 512;
        inst.drives[0].is_master = true;
        inst.drive_count = 1;
    }
}

static void select_install_type(InstallType type) {
    inst.install_type = type;
    if (type == INST_FULL) {
        for (int i = 0; i < inst.comp_count; i++) inst.components[i].selected = true;
    } else if (type == INST_MINIMAL) {
        for (int i = 0; i < inst.comp_count; i++) inst.components[i].selected = inst.components[i].required;
    }
    // CUSTOM leaves selections as-is
}

static int get_total_size() {
    int total = 0;
    for (int i = 0; i < inst.comp_count; i++) {
        if (inst.components[i].selected) total += inst.components[i].size_kb;
    }
    return total;
}

static int get_selected_count() {
    int c = 0;
    for (int i = 0; i < inst.comp_count; i++) {
        if (inst.components[i].selected) c++;
    }
    return c;
}

static void draw_progress_bar(int x, int y, int w, int pct) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('[');
    int filled = (pct * (w - 2)) / 100;
    neo::display::set_color(10, 0);
    for (int i = 0; i < filled; i++) neo::display::putchar('=');
    if (filled < w - 2) {
        neo::display::set_color(14, 0);
        neo::display::putchar('>');
        neo::display::set_color(8, 0);
        for (int i = filled + 1; i < w - 2; i++) neo::display::putchar(' ');
    }
    neo::display::set_color(7, 0);
    neo::display::putchar(']');
    char pbuf[8];
    ksprintf(pbuf, 8, " %d%%", pct);
    neo::display::puts(pbuf);
}

static void draw_step_indicator(int w) {
    const char* steps[] = {"Welcome","License","Partition","Type","Components","Install","Boot","Done"};
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    int tx = 2;
    for (int i = 0; i < NUM_STEPS; i++) {
        neo::display::set_cursor(tx, 1);
        if (i < inst.step) neo::display::set_color(10, 0);
        else if (i == inst.step) { neo::display::set_color(14, 0); neo::display::set_bold(true); }
        else neo::display::set_color(8, 0);
        neo::display::puts(steps[i]);
        neo::display::set_bold(false);
        tx += neo_strlen(steps[i]);
        if (i < NUM_STEPS - 1) { neo::display::set_color(8, 0); neo::display::puts(" > "); tx += 3; }
    }
    neo::display::set_color(7, 0);
}

static void draw_welcome() {
    int w = neo::display::get_width();
    int cx = (w - 50) / 2;

    neo::display::set_color(14, 0);
    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 5);
    neo::display::puts("Welcome to NeoInstall");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(cx, 7);
    neo::display::puts("This wizard will install NeoBench to your hard drive.");

    neo::display::set_cursor(cx, 9);
    neo::display::puts("NeoBench is a bare-metal Amiga operating environment");
    neo::display::set_cursor(cx, 10);
    neo::display::puts("featuring a modern shell, applications, games, and");
    neo::display::set_cursor(cx, 11);
    neo::display::puts("development tools for 68K Amiga computers.");

    neo::display::set_cursor(cx, 13);
    neo::display::puts("Requirements:");
    neo::display::set_cursor(cx + 2, 14);
    neo::display::puts("- Amiga with 68020+ CPU (68000 basic support)");
    neo::display::set_cursor(cx + 2, 15);
    neo::display::puts("- 1MB RAM minimum (2MB recommended)");
    neo::display::set_cursor(cx + 2, 16);
    neo::display::puts("- IDE or SCSI hard drive");
    neo::display::set_cursor(cx + 2, 17);
    neo::display::puts("- 2MB free disk space (full install)");

    neo::display::set_color(10, 0);
    neo::display::set_cursor(cx, 19);
    neo::display::puts("Press Enter to continue, Esc to cancel.");
    neo::display::set_color(7, 0);
}

static void draw_license() {
    int w = neo::display::get_width();
    int cx = 5;

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 4);
    neo::display::puts("License Agreement");
    neo::display::set_bold(false);

    const char* license[] = {
        "NeoBench Kernel - Open Source License",
        "",
        "Copyright (c) 2026 NeoBench Project",
        "",
        "Permission is hereby granted, free of charge, to any person",
        "obtaining a copy of this software and associated documentation",
        "files, to deal in the Software without restriction, including",
        "without limitation the rights to use, copy, modify, merge,",
        "publish, distribute, sublicense, and/or sell copies of the",
        "Software, and to permit persons to whom the Software is",
        "furnished to do so, subject to the following conditions:",
        "",
        "The above copyright notice and this permission notice shall",
        "be included in all copies or substantial portions of the",
        "Software.",
        "",
        "THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND.",
    };
    int num_lines = 17;

    for (int i = 0; i < num_lines && i < 15; i++) {
        int idx = inst.license_scroll + i;
        if (idx < num_lines) {
            neo::display::set_cursor(cx, 6 + i);
            neo::display::puts(license[idx]);
        }
    }

    neo::display::set_cursor(cx, 22);
    if (inst.license_accepted) {
        neo::display::set_color(10, 0);
        neo::display::puts("[X] I accept the license agreement");
    } else {
        neo::display::puts("[ ] I accept the license agreement  (Press Space to accept)");
    }
    neo::display::set_color(7, 0);
}

static void draw_partition() {
    int cx = 5;

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 4);
    neo::display::puts("Partition Editor");
    neo::display::set_bold(false);

    neo::display::set_cursor(cx, 6);
    neo::display::puts("Detected Drives:");

    for (int i = 0; i < inst.drive_count; i++) {
        neo::display::set_cursor(cx + 2, 8 + i * 3);
        if (i == inst.selected_drive) neo::display::set_color(0, 14);
        else neo::display::set_color(7, 0);

        char line[80];
        ksprintf(line, 80, "%s - %uMB (%s)", inst.drives[i].model, inst.drives[i].size_mb,
                 inst.drives[i].is_master ? "Master" : "Slave");
        neo::display::puts(line);
        neo::display::set_color(7, 0);

        // Visual drive map
        neo::display::set_cursor(cx + 4, 9 + i * 3);
        int bar_w = 40;
        neo::display::putchar('[');
        if (inst.drives[i].has_partition) {
            int pct = inst.drives[i].size_mb > 0 ?
                (int)((unsigned long long)inst.part_size_mb * bar_w / inst.drives[i].size_mb) : 0;
            neo::display::set_color(10, 0);
            for (int j = 0; j < pct; j++) neo::display::putchar('#');
            neo::display::set_color(8, 0);
            for (int j = pct; j < bar_w; j++) neo::display::putchar('.');
        } else {
            neo::display::set_color(8, 0);
            for (int j = 0; j < bar_w; j++) neo::display::putchar('.');
        }
        neo::display::set_color(7, 0);
        neo::display::putchar(']');
    }

    int py = 8 + inst.drive_count * 3 + 1;
    neo::display::set_cursor(cx, py);
    neo::display::puts("Partition Settings:");

    neo::display::set_cursor(cx + 2, py + 1);
    bool sel0 = (inst.cursor == 0);
    if (sel0) neo::display::set_color(14, 0);
    char sbuf[64];
    ksprintf(sbuf, 64, "Start: %u MB  (Left/Right to adjust)", inst.part_start_mb);
    neo::display::puts(sbuf);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(cx + 2, py + 2);
    bool sel1 = (inst.cursor == 1);
    if (sel1) neo::display::set_color(14, 0);
    ksprintf(sbuf, 64, "Size:  %u MB  (Left/Right to adjust)", inst.part_size_mb);
    neo::display::puts(sbuf);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(cx + 2, py + 4);
    bool sel2 = (inst.cursor == 2);
    if (sel2) neo::display::set_color(0, 14);
    neo::display::puts("  [ Create Partition & Format NBFS ]  ");
    neo::display::set_color(7, 0);

    if (inst.part_created) {
        neo::display::set_cursor(cx + 2, py + 6);
        neo::display::set_color(10, 0);
        neo::display::puts("Partition created and formatted!");
        neo::display::set_color(7, 0);
    }
}

static void draw_install_type() {
    int cx = 5;

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 4);
    neo::display::puts("Installation Type");
    neo::display::set_bold(false);

    struct TypeOpt { const char* name; const char* desc; int size; };
    TypeOpt types[] = {
        {"Full Install", "All components, games, development tools", get_total_size()},
        {"Minimal Install", "Core system only", 160},
        {"Custom Install", "Choose individual components", get_total_size()},
    };

    // Calculate sizes
    int full_size = 0, min_size = 0;
    for (int i = 0; i < inst.comp_count; i++) {
        full_size += inst.components[i].size_kb;
        if (inst.components[i].required) min_size += inst.components[i].size_kb;
    }
    types[0].size = full_size;
    types[1].size = min_size;

    for (int i = 0; i < 3; i++) {
        neo::display::set_cursor(cx + 2, 7 + i * 4);
        if (inst.cursor == i) neo::display::set_color(0, 14);
        else neo::display::set_color(7, 0);

        char opt;
        if (i == (int)inst.install_type) opt = '*'; else opt = ' ';
        char line[80];
        ksprintf(line, 80, " (%c) %s", opt, types[i].name);
        neo::display::puts(line);
        neo::display::set_color(7, 0);

        neo::display::set_cursor(cx + 6, 8 + i * 4);
        neo::display::set_color(8, 0);
        neo::display::puts(types[i].desc);
        neo::display::set_color(7, 0);

        neo::display::set_cursor(cx + 6, 9 + i * 4);
        ksprintf(line, 80, "Size: %d KB", types[i].size);
        neo::display::puts(line);
    }
}

static void draw_components() {
    int cx = 5;
    int h = neo::display::get_height();

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 4);
    neo::display::puts("Component Selection");
    neo::display::set_bold(false);

    char info[64];
    ksprintf(info, 64, "Selected: %d/%d components  Total: %d KB", get_selected_count(), inst.comp_count, get_total_size());
    neo::display::set_cursor(cx, 5);
    neo::display::set_color(11, 0);
    neo::display::puts(info);
    neo::display::set_color(7, 0);

    neo::display::set_color(8, 0);
    neo::display::set_cursor(cx, 6);
    neo::display::puts("Name                Category   Size    Status");
    for (int i = 0; i < 55; i++) { neo::display::set_cursor(cx + i, 7); neo::display::putchar('-'); }
    neo::display::set_color(7, 0);

    int visible = h - 12;
    if (inst.cursor < inst.scroll) inst.scroll = inst.cursor;
    if (inst.cursor >= inst.scroll + visible) inst.scroll = inst.cursor - visible + 1;

    for (int i = 0; i < visible; i++) {
        int idx = inst.scroll + i;
        if (idx >= inst.comp_count) break;

        neo::display::set_cursor(cx, 8 + i);

        bool is_cur = (idx == inst.cursor);
        if (is_cur) neo::display::set_color(0, 14);
        else if (inst.components[idx].required) neo::display::set_color(10, 0);
        else if (inst.components[idx].selected) neo::display::set_color(14, 0);
        else neo::display::set_color(7, 0);

        char check = inst.components[idx].selected ? 'X' : ' ';
        char line[80];
        ksprintf(line, 80, "[%c] %-16s %-10s %4dKB  %s",
                 check, inst.components[idx].name, inst.components[idx].category,
                 inst.components[idx].size_kb,
                 inst.components[idx].required ? "(required)" : "");
        neo::display::puts(line);
        neo::display::set_color(7, 0);
    }
}

static void draw_install_progress() {
    int w = neo::display::get_width();
    int cx = 5;

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 4);
    neo::display::puts("Installing NeoBench...");
    neo::display::set_bold(false);

    // Overall progress
    neo::display::set_cursor(cx, 7);
    neo::display::puts("Overall Progress:");
    draw_progress_bar(cx, 8, 50, inst.install_progress);

    // Current file
    neo::display::set_cursor(cx, 10);
    char fbuf[80];
    ksprintf(fbuf, 80, "File %d/%d: %s", inst.current_file, inst.total_files, inst.current_filename);
    neo::display::puts(fbuf);

    // File progress
    neo::display::set_cursor(cx, 12);
    neo::display::puts("Current File:");
    int file_pct = inst.total_files > 0 ? (inst.current_file * 100) / inst.total_files : 0;
    draw_progress_bar(cx, 13, 50, file_pct);

    // Status log
    neo::display::set_cursor(cx, 16);
    neo::display::set_color(10, 0);
    if (inst.install_progress < 10) neo::display::puts("Preparing filesystem...");
    else if (inst.install_progress < 30) neo::display::puts("Installing kernel and drivers...");
    else if (inst.install_progress < 60) neo::display::puts("Copying applications...");
    else if (inst.install_progress < 80) neo::display::puts("Installing games and tools...");
    else if (inst.install_progress < 95) neo::display::puts("Configuring system...");
    else neo::display::puts("Finalizing installation...");
    neo::display::set_color(7, 0);
}

static void draw_bootblock() {
    int w = neo::display::get_width();
    int cx = (w - 40) / 2;

    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 6);
    neo::display::puts("Boot Block Setup");
    neo::display::set_bold(false);

    neo::display::set_cursor(cx, 8);
    neo::display::puts("Writing NeoBench boot block to drive...");

    if (inst.boot_written) {
        neo::display::set_color(10, 0);
        neo::display::set_cursor(cx, 10);
        neo::display::puts("Boot block written successfully!");
        neo::display::set_color(7, 0);

        neo::display::set_cursor(cx, 12);
        neo::display::puts("Press Enter to continue.");
    } else {
        neo::display::set_cursor(cx, 10);
        neo::display::puts("Press Enter to write boot block.");
    }
}

static void draw_complete() {
    int w = neo::display::get_width();
    int cx = (w - 50) / 2;

    neo::display::set_color(10, 0);
    neo::display::set_bold(true);
    neo::display::set_cursor(cx, 6);
    neo::display::puts("Installation Complete!");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(cx, 8);
    neo::display::puts("NeoBench has been installed to your hard drive.");

    neo::display::set_cursor(cx, 10);
    char sbuf[64];
    ksprintf(sbuf, 64, "Components installed: %d", get_selected_count());
    neo::display::puts(sbuf);

    ksprintf(sbuf, 64, "Disk space used:     %d KB", get_total_size());
    neo::display::set_cursor(cx, 11);
    neo::display::puts(sbuf);

    neo::display::set_cursor(cx, 13);
    neo::display::puts("You can now:");
    neo::display::set_cursor(cx + 2, 14);
    neo::display::puts("- Remove the boot disk and restart");
    neo::display::set_cursor(cx + 2, 15);
    neo::display::puts("- The system will boot from hard drive");

    neo::display::set_cursor(cx, 18);
    bool sel0 = (inst.cursor == 0);
    bool sel1 = (inst.cursor == 1);

    if (sel0) neo::display::set_color(0, 14);
    neo::display::puts("  [ Reboot Now ]  ");
    neo::display::set_color(7, 0);
    neo::display::puts("    ");
    if (sel1) neo::display::set_color(0, 14);
    neo::display::puts("  [ Exit to Shell ]  ");
    neo::display::set_color(7, 0);
}

static void simulate_install() {
    // Simulate the installation process
    inst.total_files = get_selected_count();
    inst.current_file = 0;

    for (int i = 0; i < inst.comp_count; i++) {
        if (!inst.components[i].selected) continue;
        inst.current_file++;
        neo_strcpy(inst.current_filename, inst.components[i].name);
        inst.install_progress = (inst.current_file * 100) / inst.total_files;

        // Redraw
        neo::display::clear();
        int w = neo::display::get_width();
        int h = neo::display::get_height();

        neo::display::set_color(15, 1);
        neo::display::set_cursor(0, 0);
        for (int j = 0; j < w; j++) neo::display::putchar(' ');
        neo::display::set_cursor(2, 0);
        neo::display::set_bold(true);
        neo::display::puts("NeoInstall - Hard Drive Installer");
        neo::display::set_bold(false);
        neo::display::set_color(7, 0);

        draw_step_indicator(w);
        draw_install_progress();

        neo::display::set_color(0, 7);
        neo::display::set_cursor(0, h - 1);
        for (int j = 0; j < w; j++) neo::display::putchar(' ');
        neo::display::set_cursor(1, h - 1);
        neo::display::puts("Installing... Please wait.");
        neo::display::set_color(7, 0);

        // Simulate time per component
        neo::timer::delay_ms(200);

        // Check for cancel
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc == 0x01) return;
        }
    }

    inst.install_progress = 100;
    inst.install_complete = true;
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_color(15, 1);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("NeoInstall - Hard Drive Installer");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    draw_step_indicator(w);

    // Content
    switch (inst.step) {
        case STEP_WELCOME:    draw_welcome(); break;
        case STEP_LICENSE:    draw_license(); break;
        case STEP_PARTITION:  draw_partition(); break;
        case STEP_TYPE:       draw_install_type(); break;
        case STEP_COMPONENTS: draw_components(); break;
        case STEP_INSTALL:    draw_install_progress(); break;
        case STEP_BOOTBLOCK:  draw_bootblock(); break;
        case STEP_COMPLETE:   draw_complete(); break;
    }

    // Navigation bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);

    if (inst.step == STEP_WELCOME) {
        neo::display::puts("Enter=Next  Esc=Cancel");
    } else if (inst.step == STEP_COMPLETE) {
        neo::display::puts("Left/Right=Select  Enter=Confirm");
    } else if (inst.step == STEP_INSTALL) {
        neo::display::puts("Installing... Please wait.");
    } else {
        neo::display::puts("Enter=Next  Backspace=Back  Esc=Cancel");
    }
    neo::display::set_color(7, 0);
}

static void next_step() {
    if (inst.step == STEP_LICENSE && !inst.license_accepted) return;
    if (inst.step == STEP_PARTITION && !inst.part_created) return;

    inst.step++;
    inst.cursor = 0;
    inst.scroll = 0;

    if (inst.step == STEP_INSTALL) {
        draw_ui();
        simulate_install();
        inst.step = STEP_BOOTBLOCK;
    }
}

static void prev_step() {
    if (inst.step > 0 && inst.step != STEP_INSTALL) {
        inst.step--;
        inst.cursor = 0;
        inst.scroll = 0;
    }
}

static void handle_key(unsigned char sc) {
    char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());

    if (sc == 0x01) { inst.running = false; return; }

    // Enter
    if (ch == '\r' || ch == '\n' || sc == 0x44) {
        if (inst.step == STEP_COMPLETE) {
            inst.running = false;
            return;
        }
        if (inst.step == STEP_PARTITION && inst.cursor == 2) {
            inst.part_created = true;
            inst.drives[inst.selected_drive].has_partition = true;
            inst.drives[inst.selected_drive].formatted = true;
            return;
        }
        if (inst.step == STEP_TYPE) {
            select_install_type((InstallType)inst.cursor);
        }
        if (inst.step == STEP_BOOTBLOCK) {
            inst.boot_written = true;
            next_step();
            return;
        }
        next_step();
        return;
    }

    // Backspace
    if (sc == 0x41) { prev_step(); return; }

    // Space - toggle
    if (ch == ' ') {
        if (inst.step == STEP_LICENSE) {
            inst.license_accepted = !inst.license_accepted;
        }
        if (inst.step == STEP_COMPONENTS) {
            if (inst.cursor < inst.comp_count && !inst.components[inst.cursor].required) {
                inst.components[inst.cursor].selected = !inst.components[inst.cursor].selected;
            }
        }
        return;
    }

    // Navigation
    if (sc == 0x4C) { // Up
        if (inst.cursor > 0) inst.cursor--;
    }
    if (sc == 0x4D) { // Down
        int max_cursor = 0;
        if (inst.step == STEP_PARTITION) max_cursor = 2;
        else if (inst.step == STEP_TYPE) max_cursor = 2;
        else if (inst.step == STEP_COMPONENTS) max_cursor = inst.comp_count - 1;
        else if (inst.step == STEP_COMPLETE) max_cursor = 1;
        if (inst.cursor < max_cursor) inst.cursor++;
    }

    // Left/Right for partition size
    if (inst.step == STEP_PARTITION) {
        if (sc == 0x4F && inst.cursor == 0 && inst.part_start_mb > 0) inst.part_start_mb -= 10;
        if (sc == 0x4E && inst.cursor == 0) inst.part_start_mb += 10;
        if (sc == 0x4F && inst.cursor == 1 && inst.part_size_mb > 10) inst.part_size_mb -= 10;
        if (sc == 0x4E && inst.cursor == 1) inst.part_size_mb += 10;
        // Clamp
        unsigned int max_mb = inst.drives[inst.selected_drive].size_mb;
        if (inst.part_start_mb + inst.part_size_mb > max_mb) {
            if (inst.cursor == 0) inst.part_start_mb = max_mb - inst.part_size_mb;
            else inst.part_size_mb = max_mb - inst.part_start_mb;
        }
    }

    // Left/Right for complete screen
    if (inst.step == STEP_COMPLETE) {
        if (sc == 0x4F) inst.cursor = 0;
        if (sc == 0x4E) inst.cursor = 1;
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&inst, 0, sizeof(inst));
    inst.running = true;
    inst.step = STEP_WELCOME;

    init_components();
    detect_drives();

    // Default partition
    inst.part_start_mb = 0;
    inst.part_size_mb = inst.drives[0].size_mb > 100 ? 100 : inst.drives[0].size_mb;
    inst.install_type = INST_FULL;
    select_install_type(INST_FULL);

    draw_ui();

    while (inst.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_ui();
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("NeoInstall exited.\n");
}
