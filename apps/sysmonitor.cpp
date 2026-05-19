#include "../include/neobench.h"
#include "../lib/string.h"

// System Monitor - Real-time dashboard
// CPU usage, memory bars, uptime, disk usage, interrupt stats,
// process list, network status, auto-refresh, export

namespace {

enum MonView { MON_DASHBOARD, MON_MEMORY, MON_PROCESSES, MON_INTERRUPTS, MON_DISKS, MON_EXPORT };

struct MonState {
    bool running;
    MonView view;
    int refresh_rate; // ms
    unsigned int last_refresh;
    bool auto_refresh;

    // Sampled data
    unsigned int free_mem;
    unsigned int total_mem;
    unsigned int free_chip;
    unsigned int free_fast;
    unsigned int uptime;
    int process_count;
    neo::process::Info procs[32];
    neo::interrupts::Stats int_stats;
    neo::filesystem::MountInfo mounts[8];
    int mount_count;
    neo::cpu::CpuInfo cpu_info;

    // History for graph
    int mem_history[60];
    int hist_idx;
    int hist_count;
};

static MonState mon;

static void sample_data() {
    mon.free_mem = neo::mem::get_free_mem();
    mon.total_mem = neo::mem::get_total_mem();
    mon.free_chip = neo::mem::get_free_chip();
    mon.free_fast = neo::mem::get_free_fast();
    mon.uptime = neo::timer::get_uptime_seconds();
    mon.process_count = neo::process::list(mon.procs, 32);
    neo::interrupts::get_stats(mon.int_stats);
    mon.mount_count = neo::filesystem::list_mounts(mon.mounts, 8);
    neo::cpu::detect(mon.cpu_info);

    // Track memory usage history
    int used_pct = 0;
    if (mon.total_mem > 0) {
        used_pct = (int)(((unsigned long long)(mon.total_mem - mon.free_mem) * 100) / mon.total_mem);
    }
    mon.mem_history[mon.hist_idx] = used_pct;
    mon.hist_idx = (mon.hist_idx + 1) % 60;
    if (mon.hist_count < 60) mon.hist_count++;
}

static void draw_bar(int x, int y, int width, int pct, int fg_color) {
    int filled = (pct * width) / 100;
    neo::display::set_cursor(x, y);
    neo::display::set_color(fg_color, 0);
    for (int i = 0; i < filled; i++) neo::display::putchar('#');
    neo::display::set_color(8, 0);
    for (int i = filled; i < width; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    char pbuf[8];
    ksprintf(pbuf, 8, " %d%%", pct);
    neo::display::puts(pbuf);
}

static void draw_graph(int x, int y, int w, int h) {
    // Draw memory usage graph from history
    neo::display::set_color(8, 0);
    for (int row = 0; row < h; row++) {
        neo::display::set_cursor(x, y + row);
        neo::display::putchar('|');
    }
    neo::display::set_cursor(x, y + h);
    for (int i = 0; i <= w; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    // Y-axis labels
    neo::display::set_cursor(x - 4, y);
    neo::display::puts("100");
    neo::display::set_cursor(x - 3, y + h / 2);
    neo::display::puts("50");
    neo::display::set_cursor(x - 2, y + h);
    neo::display::puts("0");

    // Plot points
    int points = mon.hist_count;
    if (points > w) points = w;
    for (int i = 0; i < points; i++) {
        int idx = (mon.hist_idx - points + i + 60) % 60;
        int val = mon.mem_history[idx];
        int bar_h = (val * h) / 100;
        for (int r = 0; r < bar_h; r++) {
            neo::display::set_cursor(x + 1 + i, y + h - 1 - r);
            if (val > 80) neo::display::set_color(12, 0);
            else if (val > 50) neo::display::set_color(14, 0);
            else neo::display::set_color(10, 0);
            neo::display::putchar('|');
        }
    }
    neo::display::set_color(7, 0);
}

static void draw_dashboard() {
    int w = neo::display::get_width();

    // CPU Section
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    neo::display::set_color(11, 0);
    neo::display::puts("CPU");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    char cpubuf[80];
    const char* cpu_names[] = {"68000","68010","68020","68030","68040","68060"};
    int cpu_idx = mon.cpu_info.type;
    if (cpu_idx < 0 || cpu_idx > 5) cpu_idx = 0;
    ksprintf(cpubuf, 80, "  %s @ %dMHz  FPU:%s  MMU:%s",
             cpu_names[cpu_idx], mon.cpu_info.clock_mhz,
             mon.cpu_info.fpu_type ? "Yes" : "No",
             mon.cpu_info.has_mmu ? "Yes" : "No");
    neo::display::set_cursor(2, 3);
    neo::display::puts(cpubuf);

    ksprintf(cpubuf, 80, "  DCache:%s  ICache:%s  Burst:%s",
             mon.cpu_info.dcache_on ? "On" : "Off",
             mon.cpu_info.icache_on ? "On" : "Off",
             mon.cpu_info.burst_mode ? "On" : "Off");
    neo::display::set_cursor(2, 4);
    neo::display::puts(cpubuf);

    // Estimate CPU usage from process info
    int total_cpu = 0;
    for (int i = 0; i < mon.process_count; i++) total_cpu += mon.procs[i].cpu_usage;
    if (total_cpu > 100) total_cpu = 100;
    neo::display::set_cursor(2, 5);
    neo::display::puts("  Usage: ");
    draw_bar(11, 5, 30, total_cpu, total_cpu > 80 ? 12 : 10);

    // Memory Section
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 7);
    neo::display::set_color(11, 0);
    neo::display::puts("Memory");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    int mem_pct = 0;
    if (mon.total_mem > 0) mem_pct = (int)(((unsigned long long)(mon.total_mem - mon.free_mem) * 100) / mon.total_mem);

    char mbuf[80];
    ksprintf(mbuf, 80, "  Total: %uK  Free: %uK  Used: %uK",
             mon.total_mem / 1024, mon.free_mem / 1024, (mon.total_mem - mon.free_mem) / 1024);
    neo::display::set_cursor(2, 8);
    neo::display::puts(mbuf);

    neo::display::set_cursor(2, 9);
    neo::display::puts("  Total:  ");
    draw_bar(12, 9, 30, mem_pct, mem_pct > 80 ? 12 : 14);

    // Chip/Fast breakdown
    unsigned int total_chip = mon.free_chip + (mon.total_mem - mon.free_mem) / 2; // estimate
    int chip_pct = total_chip > 0 ? (int)(((unsigned long long)(total_chip - mon.free_chip) * 100) / total_chip) : 0;
    if (chip_pct > 100) chip_pct = 100;
    ksprintf(mbuf, 80, "  Chip: %uK free", mon.free_chip / 1024);
    neo::display::set_cursor(2, 10);
    neo::display::puts(mbuf);
    neo::display::set_cursor(2, 11);
    neo::display::puts("  Chip:   ");
    draw_bar(12, 11, 30, chip_pct, 13);

    ksprintf(mbuf, 80, "  Fast: %uK free", mon.free_fast / 1024);
    neo::display::set_cursor(2, 12);
    neo::display::puts(mbuf);

    // Uptime
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 14);
    neo::display::set_color(11, 0);
    neo::display::puts("System");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    ksprintf(mbuf, 80, "  Uptime: %dd %dh %dm %ds",
             mon.uptime / 86400, (mon.uptime / 3600) % 24, (mon.uptime / 60) % 60, mon.uptime % 60);
    neo::display::set_cursor(2, 15);
    neo::display::puts(mbuf);

    ksprintf(mbuf, 80, "  Processes: %d   Interrupts: %u", mon.process_count, mon.int_stats.total);
    neo::display::set_cursor(2, 16);
    neo::display::puts(mbuf);

    // RTC
    if (neo::rtc::is_present()) {
        neo::rtc::DateTime dt;
        neo::rtc::read(dt);
        ksprintf(mbuf, 80, "  Date: %d/%d/%d %02d:%02d:%02d", dt.month, dt.day, dt.year, dt.hour, dt.minute, dt.second);
        neo::display::set_cursor(2, 17);
        neo::display::puts(mbuf);
    }

    // Disk usage
    if (mon.mount_count > 0) {
        neo::display::set_bold(true);
        neo::display::set_cursor(w / 2, 2);
        neo::display::set_color(11, 0);
        neo::display::puts("Disk Usage");
        neo::display::set_bold(false);
        neo::display::set_color(7, 0);

        for (int i = 0; i < mon.mount_count && i < 4; i++) {
            int y = 3 + i * 3;
            ksprintf(mbuf, 80, "  %s (%s)", mon.mounts[i].mount_point, mon.mounts[i].fs_type);
            neo::display::set_cursor(w / 2, y);
            neo::display::puts(mbuf);

            unsigned int total_kb = (unsigned long long)mon.mounts[i].total_blocks * mon.mounts[i].block_size / 1024;
            unsigned int free_kb = (unsigned long long)mon.mounts[i].free_blocks * mon.mounts[i].block_size / 1024;
            int disk_pct = total_kb > 0 ? (int)(((unsigned long long)(total_kb - free_kb) * 100) / total_kb) : 0;

            ksprintf(mbuf, 80, "  %uK / %uK", total_kb - free_kb, total_kb);
            neo::display::set_cursor(w / 2, y + 1);
            neo::display::puts(mbuf);
            neo::display::set_cursor(w / 2, y + 2);
            neo::display::puts("  ");
            draw_bar(w / 2 + 2, y + 2, 25, disk_pct, disk_pct > 90 ? 12 : 10);
        }
    }

    // Mini memory graph
    if (w > 60) {
        neo::display::set_bold(true);
        neo::display::set_cursor(w / 2, 15);
        neo::display::set_color(11, 0);
        neo::display::puts("Memory History");
        neo::display::set_bold(false);
        neo::display::set_color(7, 0);
        draw_graph(w / 2 + 4, 16, 30, 5);
    }
}

static void draw_processes_view() {
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    neo::display::puts("Process List");
    neo::display::set_bold(false);

    neo::display::set_color(11, 0);
    neo::display::set_cursor(2, 4);
    neo::display::puts("PID  Name                 State  Pri  CPU%%  Stack");
    neo::display::set_color(8, 0);
    neo::display::set_cursor(2, 5);
    for (int i = 0; i < 60; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    const char* state_names[] = {"Run","Wait","Sleep","Stop","Dead"};

    for (int i = 0; i < mon.process_count && i < 20; i++) {
        char line[80];
        int st = mon.procs[i].state;
        if (st < 0 || st > 4) st = 0;

        ksprintf(line, 80, "%3d  %-20s %-5s  %3d  %3d%%  %u/%u",
                 mon.procs[i].pid, mon.procs[i].name, state_names[st],
                 mon.procs[i].priority, mon.procs[i].cpu_usage,
                 mon.procs[i].stack_used, mon.procs[i].stack_size);
        neo::display::set_cursor(2, 6 + i);

        if (mon.procs[i].cpu_usage > 50) neo::display::set_color(12, 0);
        else if (mon.procs[i].state == 0) neo::display::set_color(10, 0);
        else neo::display::set_color(7, 0);

        neo::display::puts(line);
    }
    neo::display::set_color(7, 0);
}

static void draw_interrupts_view() {
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    neo::display::puts("Interrupt Statistics");
    neo::display::set_bold(false);

    const char* level_names[] = {
        "Level 0 (unused)", "Level 1 (Software)", "Level 2 (CIA-A/Ports)",
        "Level 3 (VBlank/Copper)", "Level 4 (Audio)", "Level 5 (Disk/Serial)",
        "Level 6 (CIA-B/INTEN)", "Level 7 (NMI)"
    };

    neo::display::set_color(11, 0);
    neo::display::set_cursor(2, 4);
    neo::display::puts("Level   Description              Count");
    neo::display::set_color(8, 0);
    neo::display::set_cursor(2, 5);
    for (int i = 0; i < 50; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    for (int i = 0; i < 8; i++) {
        char line[80];
        ksprintf(line, 80, "  %d     %-25s %u", i, level_names[i], mon.int_stats.level_counts[i]);
        neo::display::set_cursor(2, 6 + i);
        if (mon.int_stats.level_counts[i] > 0) neo::display::set_color(10, 0);
        else neo::display::set_color(8, 0);
        neo::display::puts(line);
    }

    neo::display::set_color(7, 0);
    char tbuf[64];
    ksprintf(tbuf, 64, "  Total: %u   Spurious: %u", mon.int_stats.total, mon.int_stats.spurious);
    neo::display::set_cursor(2, 15);
    neo::display::puts(tbuf);

    // VBlank counter
    unsigned int vbl = neo::intr::get_vblank_count();
    ksprintf(tbuf, 64, "  VBlank count: %u   Ticks: %u", vbl, neo::intr::get_ticks());
    neo::display::set_cursor(2, 17);
    neo::display::puts(tbuf);
}

static void draw_memory_view() {
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    neo::display::puts("Memory Details");
    neo::display::set_bold(false);

    char buf[80];
    ksprintf(buf, 80, "Total Memory:     %u bytes (%uK)", mon.total_mem, mon.total_mem / 1024);
    neo::display::set_cursor(4, 4);
    neo::display::puts(buf);

    ksprintf(buf, 80, "Free Memory:      %u bytes (%uK)", mon.free_mem, mon.free_mem / 1024);
    neo::display::set_cursor(4, 5);
    neo::display::puts(buf);

    ksprintf(buf, 80, "Used Memory:      %u bytes (%uK)", mon.total_mem - mon.free_mem, (mon.total_mem - mon.free_mem) / 1024);
    neo::display::set_cursor(4, 6);
    neo::display::puts(buf);

    ksprintf(buf, 80, "Free Chip RAM:    %u bytes (%uK)", mon.free_chip, mon.free_chip / 1024);
    neo::display::set_cursor(4, 8);
    neo::display::set_color(13, 0);
    neo::display::puts(buf);
    neo::display::set_color(7, 0);

    ksprintf(buf, 80, "Free Fast RAM:    %u bytes (%uK)", mon.free_fast, mon.free_fast / 1024);
    neo::display::set_cursor(4, 9);
    neo::display::set_color(14, 0);
    neo::display::puts(buf);
    neo::display::set_color(7, 0);

    // Visual memory map
    neo::display::set_bold(true);
    neo::display::set_cursor(4, 11);
    neo::display::puts("Memory Map:");
    neo::display::set_bold(false);

    int bar_w = 50;
    int mem_pct = mon.total_mem > 0 ? (int)(((unsigned long long)(mon.total_mem - mon.free_mem) * 100) / mon.total_mem) : 0;
    neo::display::set_cursor(4, 13);
    neo::display::puts("  Overall: [");
    draw_bar(16, 13, bar_w, mem_pct, mem_pct > 80 ? 12 : 10);
    neo::display::puts("]");

    // Memory usage graph
    neo::display::set_bold(true);
    neo::display::set_cursor(4, 16);
    neo::display::puts("Usage Over Time:");
    neo::display::set_bold(false);
    draw_graph(8, 17, 50, 6);
}

static void export_stats() {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, "SYS:sysmon.log", neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) return;

    char buf[INODE_SIZE];
    ksprintf(buf, INODE_SIZE, "NeoBench System Monitor Export\n");
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Uptime: %u seconds\n", mon.uptime);
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Memory: %u/%u bytes free\n", mon.free_mem, mon.total_mem);
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Chip RAM free: %u\n", mon.free_chip);
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Fast RAM free: %u\n", mon.free_fast);
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Processes: %d\n", mon.process_count);
    neo::filesystem::write(fh, buf, neo_strlen(buf));
    ksprintf(buf, INODE_SIZE, "Interrupts total: %u\n", mon.int_stats.total);
    neo::filesystem::write(fh, buf, neo_strlen(buf));

    neo::filesystem::close(fh);
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_color(15, 2);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("System Monitor");
    neo::display::set_bold(false);

    const char* tabs[] = {"[1]Dashboard","[2]Memory","[3]Procs","[4]Interrupts","[5]Disks"};
    int tx = 20;
    for (int i = 0; i < 5; i++) {
        neo::display::set_cursor(tx, 0);
        if ((int)mon.view == i) neo::display::set_color(14, 2);
        else neo::display::set_color(7, 2);
        neo::display::puts(tabs[i]);
        tx += neo_strlen(tabs[i]) + 2;
    }
    neo::display::set_color(7, 0);

    switch (mon.view) {
        case MON_DASHBOARD:  draw_dashboard(); break;
        case MON_MEMORY:     draw_memory_view(); break;
        case MON_PROCESSES:  draw_processes_view(); break;
        case MON_INTERRUPTS: draw_interrupts_view(); break;
        case MON_DISKS:      draw_dashboard(); break; // reuse disk section
        case MON_EXPORT:     break;
    }

    // Auto-refresh indicator
    neo::display::set_cursor(w - 15, 0);
    neo::display::set_color(mon.auto_refresh ? 10 : 12, 2);
    neo::display::puts(mon.auto_refresh ? "Auto:ON " : "Auto:OFF");
    neo::display::set_color(7, 0);

    // Status bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("1-5=View  R=Refresh  A=AutoRefresh  E=Export  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());

    if (sc == 0x01) { mon.running = false; return; }

    if (ch == '1') mon.view = MON_DASHBOARD;
    if (ch == '2') mon.view = MON_MEMORY;
    if (ch == '3') mon.view = MON_PROCESSES;
    if (ch == '4') mon.view = MON_INTERRUPTS;
    if (ch == '5') mon.view = MON_DISKS;

    if (ch == 'r' || ch == 'R') { sample_data(); }
    if (ch == 'a' || ch == 'A') { mon.auto_refresh = !mon.auto_refresh; }
    if (ch == 'e' || ch == 'E') { export_stats(); }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&mon, 0, sizeof(mon));
    mon.running = true;
    mon.auto_refresh = true;
    mon.refresh_rate = 1000;

    sample_data();
    draw_ui();

    while (mon.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            sample_data();
            draw_ui();
        }

        if (mon.auto_refresh) {
            unsigned int now = neo::timer::get_ticks();
            if (now - mon.last_refresh >= (unsigned int)mon.refresh_rate) {
                sample_data();
                draw_ui();
                mon.last_refresh = now;
            }
        }

        neo::timer::delay_ms(50);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("System Monitor exited.\n");
}
