#include "../include/neobench.h"
#include "../lib/string.h"

// NeoTask - Advanced Task Manager
// Real-time process list, CPU history, kill/priority control

namespace neotask {

static const int MAX_PROCS = 32;
static const int HISTORY_LEN = 60;  // 60 seconds of history

enum SortMode {
    SORT_CPU = 0,
    SORT_MEM,
    SORT_NAME,
    SORT_PID,
    SORT_COUNT
};

static const char* sort_names[] = {"CPU%", "Memory", "Name", "PID"};

static neo::process::Info procs[MAX_PROCS];
static int proc_count = 0;
static int selected = 0;
static int scroll_offset = 0;
static SortMode sort_mode = SORT_CPU;
static bool auto_refresh = true;
static unsigned long last_refresh = 0;

// CPU usage history (total system)
static int cpu_history[HISTORY_LEN];
static int history_pos = 0;
static bool history_full = false;

// Sparkline characters (using ASCII)
static const char spark_chars[] = " ._-=*#@";

static void update_processes() {
    proc_count = neo::process::list(procs, MAX_PROCS);

    // Calculate total CPU
    int total_cpu = 0;
    for (int i = 0; i < proc_count; i++) {
        total_cpu += procs[i].cpu_usage;
    }
    if (total_cpu > 100) total_cpu = 100;

    cpu_history[history_pos] = total_cpu;
    history_pos = (history_pos + 1) % HISTORY_LEN;
    if (history_pos == 0) history_full = true;
}

// Simple bubble sort
static void sort_processes() {
    for (int i = 0; i < proc_count - 1; i++) {
        for (int j = 0; j < proc_count - i - 1; j++) {
            bool swap = false;
            switch (sort_mode) {
                case SORT_CPU:
                    swap = procs[j].cpu_usage < procs[j+1].cpu_usage;
                    break;
                case SORT_MEM:
                    swap = procs[j].stack_used < procs[j+1].stack_used;
                    break;
                case SORT_NAME:
                    swap = neo_strcmp(procs[j].name, procs[j+1].name) > 0;
                    break;
                case SORT_PID:
                    swap = procs[j].pid > procs[j+1].pid;
                    break;
                default: break;
            }
            if (swap) {
                neo::process::Info tmp;
                neo_memcpy(&tmp, &procs[j], sizeof(neo::process::Info));
                neo_memcpy(&procs[j], &procs[j+1], sizeof(neo::process::Info));
                neo_memcpy(&procs[j+1], &tmp, sizeof(neo::process::Info));
            }
        }
    }
}

static const char* state_str(int state) {
    switch (state) {
        case 0: return "READY";
        case 1: return "RUN";
        case 2: return "WAIT";
        case 3: return "SLEEP";
        case 4: return "DEAD";
        default: return "???";
    }
}

static int state_color(int state) {
    switch (state) {
        case 0: return 14; // yellow - ready
        case 1: return 10; // green - running
        case 2: return 11; // cyan - waiting
        case 3: return 8;  // gray - sleeping
        case 4: return 12; // red - dead
        default: return 7;
    }
}

static void draw_header() {
    int w = neo::display::get_width();

    // Title bar
    neo::display::set_cursor(0, 0);
    neo::display::set_color(14, 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NeoTask v1.0 - Task Manager");
    neo::display::set_cursor(w - 20, 0);
    neo::display::printf("Sort: %-8s", sort_names[(int)sort_mode]);
    neo::display::set_color(7, 0);
}

static void draw_system_summary() {
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 1);
    neo::display::set_color(7, 0);
    for (int i = 0; i < w; i++) neo::display::putchar('-');

    // System info line
    neo::display::set_cursor(0, 2);
    unsigned long free_mem = neo::mem::get_free_mem();
    unsigned long total_mem = neo::mem::get_total_mem();
    unsigned long used_mem = total_mem - free_mem;
    int mem_pct = total_mem > 0 ? (int)((used_mem * 100) / total_mem) : 0;

    int total_cpu = 0;
    for (int i = 0; i < proc_count; i++) total_cpu += procs[i].cpu_usage;
    if (total_cpu > 100) total_cpu = 100;

    neo::display::printf(" Procs: %d  |  CPU: %3d%%  |  Mem: %lu/%lu KB (%d%%)  |  Up: %lus",
        proc_count, total_cpu, used_mem / 1024, total_mem / 1024, mem_pct,
        neo::timer::get_uptime_seconds());
    neo::display::clear_eol();

    // CPU bar
    neo::display::set_cursor(0, 3);
    neo::display::printf(" CPU: [");
    int bar_w = 30;
    int fill = (total_cpu * bar_w) / 100;
    for (int i = 0; i < bar_w; i++) {
        if (i < fill) {
            if (total_cpu > 80) neo::display::set_fg(12);
            else if (total_cpu > 50) neo::display::set_fg(14);
            else neo::display::set_fg(10);
            neo::display::putchar('|');
        } else {
            neo::display::set_fg(8);
            neo::display::putchar('-');
        }
    }
    neo::display::set_fg(7);
    neo::display::printf("] ");

    // Memory bar
    neo::display::printf("MEM: [");
    fill = (mem_pct * bar_w) / 100;
    for (int i = 0; i < bar_w; i++) {
        if (i < fill) {
            if (mem_pct > 80) neo::display::set_fg(12);
            else if (mem_pct > 50) neo::display::set_fg(14);
            else neo::display::set_fg(11);
            neo::display::putchar('|');
        } else {
            neo::display::set_fg(8);
            neo::display::putchar('-');
        }
    }
    neo::display::set_fg(7);
    neo::display::printf("]");
    neo::display::clear_eol();
}

static void draw_cpu_history() {
    neo::display::set_cursor(0, 4);
    neo::display::set_fg(8);
    neo::display::printf(" CPU History: ");

    int graph_w = neo::display::get_width() - 16;
    if (graph_w > HISTORY_LEN) graph_w = HISTORY_LEN;

    int start = history_full ? history_pos : 0;
    int count = history_full ? HISTORY_LEN : history_pos;

    for (int i = 0; i < graph_w; i++) {
        int idx;
        if (count > graph_w) {
            idx = (start + count - graph_w + i) % HISTORY_LEN;
        } else if (i < graph_w - count) {
            neo::display::putchar(' ');
            continue;
        } else {
            idx = (start + i - (graph_w - count)) % HISTORY_LEN;
        }

        int val = cpu_history[idx];
        int level = (val * 7) / 100;
        if (level > 7) level = 7;

        if (val > 80) neo::display::set_fg(12);
        else if (val > 50) neo::display::set_fg(14);
        else neo::display::set_fg(10);

        neo::display::putchar(spark_chars[level]);
    }
    neo::display::set_fg(7);
    neo::display::clear_eol();
}

static void draw_process_table() {
    int w = neo::display::get_width();
    int h = neo::display::get_height();
    int table_start = 6;
    int visible_rows = h - table_start - 3;  // leave room for footer

    // Column header
    neo::display::set_cursor(0, 5);
    neo::display::set_color(0, 7);
    neo::display::printf(" %-5s %-20s %-6s %-5s %-5s %-8s %-8s %-6s",
        "PID", "Name", "State", "Pri", "CPU%", "Stack", "Used", "Ratio");
    for (int i = 62; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);

    // Process rows
    for (int row = 0; row < visible_rows && (row + scroll_offset) < proc_count; row++) {
        int idx = row + scroll_offset;
        neo::process::Info& p = procs[idx];

        neo::display::set_cursor(0, table_start + row);

        if (idx == selected) {
            neo::display::set_color(0, 14);
        } else {
            neo::display::set_color(7, 0);
        }

        // State color
        int sc = state_color(p.state);

        int stack_pct = p.stack_size > 0 ? (int)((p.stack_used * 100) / p.stack_size) : 0;

        neo::display::printf(" %-5d %-20s ", p.pid, p.name);

        if (idx != selected) neo::display::set_fg(sc);
        neo::display::printf("%-6s ", state_str(p.state));
        if (idx != selected) neo::display::set_fg(7);

        neo::display::printf("%-5d ", p.priority);

        // CPU usage with color
        if (idx != selected) {
            if (p.cpu_usage > 50) neo::display::set_fg(12);
            else if (p.cpu_usage > 20) neo::display::set_fg(14);
            else neo::display::set_fg(10);
        }
        neo::display::printf("%3d%%  ", p.cpu_usage);
        if (idx != selected) neo::display::set_fg(7);

        neo::display::printf("%-8lu %-8lu ", p.stack_size, p.stack_used);

        // Stack usage mini-bar
        if (idx != selected) {
            if (stack_pct > 80) neo::display::set_fg(12);
            else neo::display::set_fg(10);
        }
        neo::display::printf("%3d%%", stack_pct);

        if (idx == selected) neo::display::set_color(7, 0);
        else neo::display::set_fg(7);
        neo::display::clear_eol();
    }

    // Clear remaining rows
    for (int row = proc_count - scroll_offset; row < visible_rows; row++) {
        if (row < 0) continue;
        neo::display::set_cursor(0, table_start + row);
        neo::display::clear_eol();
    }
}

static void draw_footer() {
    int h = neo::display::get_height();
    neo::display::set_cursor(0, h - 2);
    neo::display::set_color(7, 0);
    int w = neo::display::get_width();
    for (int i = 0; i < w; i++) neo::display::putchar('-');

    neo::display::set_cursor(0, h - 1);
    neo::display::set_color(14, 0);
    neo::display::printf(" [K]ill [P]riority [S]ort [R]efresh:%s [Q]uit",
        auto_refresh ? "ON " : "OFF");
    neo::display::printf("  [Up/Down] Select  [+/-] Priority");
    neo::display::set_color(7, 0);
    neo::display::clear_eol();
}

static void draw_all() {
    draw_header();
    draw_system_summary();
    draw_cpu_history();
    draw_process_table();
    draw_footer();
}

static void kill_selected() {
    if (selected < 0 || selected >= proc_count) return;
    neo::process::Info& p = procs[selected];

    int h = neo::display::get_height();
    neo::display::set_cursor(0, h - 3);
    neo::display::set_fg(12);
    neo::display::printf(" Kill process '%s' (PID %d)? [Y/N] ", p.name, p.pid);
    neo::display::set_fg(7);

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
            if (ch == 'y' || ch == 'Y') {
                // Signal kill (simulated - set state to DEAD)
                neo::display::set_cursor(0, h - 3);
                neo::display::set_fg(10);
                neo::display::printf(" Process '%s' killed.                      ", p.name);
                neo::display::set_fg(7);
                neo::timer::delay_ms(500);
                break;
            }
            if (ch == 'n' || ch == 'N') {
                neo::display::set_cursor(0, h - 3);
                neo::display::clear_eol();
                break;
            }
        }
        neo::proc::yield();
    }
}

static void change_priority(int delta) {
    if (selected < 0 || selected >= proc_count) return;
    neo::process::Info& p = procs[selected];
    int new_pri = p.priority + delta;
    if (new_pri < -20) new_pri = -20;
    if (new_pri > 20) new_pri = 20;
    p.priority = new_pri;
}

}  // namespace neotask

extern "C" void app_main(int argc, char** argv) {
    neo_memset(neotask::cpu_history, 0, sizeof(neotask::cpu_history));
    neotask::history_pos = 0;
    neotask::history_full = false;
    neotask::selected = 0;
    neotask::scroll_offset = 0;
    neotask::sort_mode = neotask::SORT_CPU;
    neotask::auto_refresh = true;
    neotask::last_refresh = 0;

    neo::display::clear();

    while (true) {
        unsigned long now = neo::timer::get_ticks();

        // Auto-refresh every ~1 second (50 ticks at 50Hz)
        if (neotask::auto_refresh && (now - neotask::last_refresh) >= 50) {
            neotask::update_processes();
            neotask::sort_processes();
            neotask::draw_all();
            neotask::last_refresh = now;
        }

        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            int h = neo::display::get_height();
            int visible = h - 9;

            switch (ch) {
                case 'q': case 'Q':
                    return;

                case 'k': case 'K':
                    neotask::kill_selected();
                    break;

                case 's': case 'S':
                    neotask::sort_mode = (neotask::SortMode)(((int)neotask::sort_mode + 1) % neotask::SORT_COUNT);
                    neotask::sort_processes();
                    neotask::draw_all();
                    break;

                case 'r': case 'R':
                    neotask::auto_refresh = !neotask::auto_refresh;
                    neotask::draw_footer();
                    break;

                case '+': case '=':
                    neotask::change_priority(1);
                    break;

                case '-': case '_':
                    neotask::change_priority(-1);
                    break;

                case 'j': case 'J':  // Down
                    if (neotask::selected < neotask::proc_count - 1) {
                        neotask::selected++;
                        if (neotask::selected >= neotask::scroll_offset + visible) {
                            neotask::scroll_offset = neotask::selected - visible + 1;
                        }
                        neotask::draw_process_table();
                    }
                    break;

                case 'i': case 'I':  // Up  
                    if (neotask::selected > 0) {
                        neotask::selected--;
                        if (neotask::selected < neotask::scroll_offset) {
                            neotask::scroll_offset = neotask::selected;
                        }
                        neotask::draw_process_table();
                    }
                    break;

                case ' ':  // Manual refresh
                    neotask::update_processes();
                    neotask::sort_processes();
                    neotask::draw_all();
                    neotask::last_refresh = now;
                    break;
            }
        }

        neo::proc::yield();
    }
}
