#include "../include/neobench.h"
#include "../lib/string.h"

// NeoMan - Package Manager with Pac-Man theme
// Commands: list, install, remove, search, info, update
// .npk package format with dependency resolution

namespace neoman {

static const int MAX_PACKAGES = 64;
static const int MAX_DEPS = 8;
static const int MAX_NAME = 32;
static const int MAX_DESC = 128;

struct Package {
    char name[MAX_NAME];
    char version[16];
    char description[MAX_DESC];
    char author[32];
    unsigned long size_kb;
    char deps[MAX_DEPS][MAX_NAME];
    int dep_count;
    bool installed;
};

static Package repo[MAX_PACKAGES];
static int repo_count = 0;

// Pac-Man animation state
static int pacman_x = 0;
static int pacman_frame = 0;
static const int ANIM_ROW = 14;

// --- Built-in package repository ---
static void init_repo() {
    repo_count = 0;

    auto add = [](const char* name, const char* ver, const char* desc,
                  const char* author, unsigned long sz, bool inst,
                  const char* d1 = nullptr, const char* d2 = nullptr) {
        Package& p = repo[repo_count++];
        neo_strcpy(p.name, name);
        neo_strcpy(p.version, ver);
        neo_strcpy(p.description, desc);
        neo_strcpy(p.author, author);
        p.size_kb = sz;
        p.installed = inst;
        p.dep_count = 0;
        if (d1) { neo_strcpy(p.deps[p.dep_count++], d1); }
        if (d2) { neo_strcpy(p.deps[p.dep_count++], d2); }
    };

    add("neo-core",    "1.0.0", "Core system libraries",              "NeoBench Team", 128, true);
    add("neo-shell",   "1.0.0", "Command line shell",                 "NeoBench Team", 64,  true, "neo-core");
    add("neo-fs",      "1.0.0", "Filesystem utilities",               "NeoBench Team", 48,  true, "neo-core");
    add("neo-net",     "0.9.0", "Network stack and utilities",        "NeoBench Team", 96,  false, "neo-core");
    add("neo-edit",    "1.2.0", "Text editor",                        "NeoBench Team", 52,  true, "neo-core", "neo-fs");
    add("neo-calc",    "1.0.0", "Calculator application",             "NeoBench Team", 24,  false, "neo-core");
    add("neo-games",   "1.1.0", "Game collection",                    "NeoBench Team", 180, false, "neo-core");
    add("neo-music",   "0.8.0", "MOD player and audio tools",        "NeoBench Team", 72,  false, "neo-core");
    add("neo-gfx",     "1.0.0", "Graphics drawing library",          "NeoBench Team", 156, false, "neo-core");
    add("neo-dev",     "1.0.0", "Development tools and assembler",   "NeoBench Team", 210, false, "neo-core", "neo-fs");
    add("neo-bench",   "1.0.0", "Benchmarking suite",                "NeoBench Team", 44,  false, "neo-core");
    add("neo-theme",   "1.0.0", "Theme engine",                      "NeoBench Team", 20,  false, "neo-core");
    add("neo-help",    "1.0.0", "Documentation browser",             "NeoBench Team", 36,  false, "neo-core");
    add("neo-script",  "0.5.0", "Scripting language interpreter",    "NeoBench Team", 88,  false, "neo-core", "neo-fs");
    add("neo-zip",     "1.0.0", "Archive manager",                   "NeoBench Team", 60,  false, "neo-core", "neo-fs");
    add("neo-find",    "1.0.0", "File search utility",               "NeoBench Team", 28,  false, "neo-core", "neo-fs");
}

static int find_package(const char* name) {
    for (int i = 0; i < repo_count; i++) {
        if (neo_strcmp(repo[i].name, name) == 0) return i;
    }
    return -1;
}

// --- Pac-Man eating animation ---
static void draw_pacman_line(int dots_total, int dots_eaten) {
    neo::display::set_cursor(0, ANIM_ROW);
    neo::display::clear_eol();
    neo::display::set_cursor(0, ANIM_ROW);

    int w = neo::display::get_width() - 4;
    if (dots_total > w) dots_total = w;

    neo::display::printf("  ");

    for (int i = 0; i < dots_total; i++) {
        if (i < dots_eaten) {
            neo::display::putchar(' ');
        } else if (i == dots_eaten) {
            // Pac-Man character
            neo::display::set_fg(14); // Yellow
            if (pacman_frame & 1)
                neo::display::putchar('C');
            else
                neo::display::putchar('O');
            neo::display::set_fg(7);
        } else {
            neo::display::set_fg(15);
            neo::display::putchar('.');
            neo::display::set_fg(7);
        }
    }
}

static void animate_install(const char* pkg_name, unsigned long size_kb) {
    int dots = 40;
    neo::display::set_cursor(2, ANIM_ROW - 2);
    neo::display::set_fg(11);
    neo::display::printf("Installing %s (%lu KB)...", pkg_name, size_kb);
    neo::display::set_fg(7);

    for (int i = 0; i <= dots; i++) {
        pacman_frame++;
        draw_pacman_line(dots, i);

        // Progress percentage
        int pct = (i * 100) / dots;
        neo::display::set_cursor(2, ANIM_ROW + 1);
        neo::display::printf("  Progress: %3d%%", pct);
        neo::display::clear_eol();

        neo::timer::delay_ms(30 + (size_kb / 20));
    }

    neo::display::set_cursor(2, ANIM_ROW + 2);
    neo::display::set_fg(10);
    neo::display::printf("  Wakka wakka! %s installed successfully!", pkg_name);
    neo::display::set_fg(7);
}

static void animate_remove(const char* pkg_name) {
    neo::display::set_cursor(2, ANIM_ROW - 2);
    neo::display::set_fg(12);
    neo::display::printf("Removing %s...", pkg_name);
    neo::display::set_fg(7);

    // Ghost eating animation (reverse)
    int dots = 30;
    for (int i = dots; i >= 0; i--) {
        pacman_frame++;
        neo::display::set_cursor(2, ANIM_ROW);
        neo::display::clear_eol();
        neo::display::set_cursor(2, ANIM_ROW);
        for (int j = 0; j < dots; j++) {
            if (j > i) {
                neo::display::putchar(' ');
            } else if (j == i) {
                neo::display::set_fg(12); // Red ghost
                neo::display::putchar('M');
                neo::display::set_fg(7);
            } else {
                neo::display::set_fg(8);
                neo::display::putchar('#');
                neo::display::set_fg(7);
            }
        }
        neo::timer::delay_ms(40);
    }

    neo::display::set_cursor(2, ANIM_ROW + 1);
    neo::display::set_fg(10);
    neo::display::printf("  Package removed.");
    neo::display::set_fg(7);
}

// --- Topological sort for dependency resolution ---
static int install_order[MAX_PACKAGES];
static int install_order_count = 0;
static bool visited[MAX_PACKAGES];
static bool in_stack[MAX_PACKAGES];

static bool topo_visit(int idx) {
    if (in_stack[idx]) {
        kprintf("NeoMan: Circular dependency on %s\n", repo[idx].name);
        return false;
    }
    if (visited[idx]) return true;

    visited[idx] = true;
    in_stack[idx] = true;

    Package& p = repo[idx];
    for (int d = 0; d < p.dep_count; d++) {
        int di = find_package(p.deps[d]);
        if (di >= 0 && !repo[di].installed) {
            if (!topo_visit(di)) return false;
        }
    }

    in_stack[idx] = false;
    install_order[install_order_count++] = idx;
    return true;
}

static bool resolve_deps(int pkg_idx) {
    install_order_count = 0;
    neo_memset(visited, 0, sizeof(visited));
    neo_memset(in_stack, 0, sizeof(in_stack));
    return topo_visit(pkg_idx);
}

// --- UI Helpers ---
static void draw_header() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NeoMan v1.0 - Package Manager  C < . . . . .");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
}

static void wait_key() {
    neo::display::printf("\n  Press any key...");
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

// --- Commands ---
static void cmd_list() {
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Installed Packages:\n\n");
    neo::display::set_color(7, 0);

    int count = 0;
    for (int i = 0; i < repo_count; i++) {
        if (repo[i].installed) {
            neo::display::printf("  %-16s %-8s  %4lu KB  %s\n",
                repo[i].name, repo[i].version, repo[i].size_kb, repo[i].description);
            count++;
        }
    }

    neo::display::printf("\n  %d package(s) installed.\n", count);

    unsigned long total = 0;
    for (int i = 0; i < repo_count; i++)
        if (repo[i].installed) total += repo[i].size_kb;
    neo::display::printf("  Total disk usage: %lu KB\n", total);
    wait_key();
}

static void cmd_search(const char* query) {
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Search results for '%s':\n\n", query);
    neo::display::set_color(7, 0);

    int found = 0;
    int qlen = neo_strlen(query);

    for (int i = 0; i < repo_count; i++) {
        // Simple substring match in name or description
        bool match = false;
        int nlen = neo_strlen(repo[i].name);
        for (int j = 0; j <= nlen - qlen; j++) {
            if (neo_strncmp(&repo[i].name[j], query, qlen) == 0) {
                match = true; break;
            }
        }
        if (!match) {
            int dlen = neo_strlen(repo[i].description);
            for (int j = 0; j <= dlen - qlen; j++) {
                if (neo_strncmp(&repo[i].description[j], query, qlen) == 0) {
                    match = true; break;
                }
            }
        }

        if (match) {
            neo::display::printf("  %c %-16s %-8s  %s\n",
                repo[i].installed ? '*' : ' ',
                repo[i].name, repo[i].version, repo[i].description);
            found++;
        }
    }

    if (found == 0) {
        neo::display::set_fg(12);
        neo::display::printf("  No packages found matching '%s'.\n", query);
        neo::display::set_fg(7);
    } else {
        neo::display::printf("\n  %d result(s). (* = installed)\n", found);
    }
    wait_key();
}

static void cmd_info(const char* name) {
    draw_header();
    int idx = find_package(name);
    neo::display::set_cursor(0, 3);

    if (idx < 0) {
        neo::display::set_fg(12);
        neo::display::printf("  Package '%s' not found.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    Package& p = repo[idx];
    neo::display::set_color(15, 0);
    neo::display::printf("  Package Information\n");
    neo::display::set_color(7, 0);
    neo::display::printf("  +------------------------------------------+\n");
    neo::display::printf("  | Name:        %-28s |\n", p.name);
    neo::display::printf("  | Version:     %-28s |\n", p.version);
    neo::display::printf("  | Author:      %-28s |\n", p.author);
    neo::display::printf("  | Size:        %-24lu KB |\n", p.size_kb);
    neo::display::printf("  | Status:      %-28s |\n", p.installed ? "Installed" : "Not installed");
    neo::display::printf("  +------------------------------------------+\n");
    neo::display::printf("  | Description:                             |\n");
    neo::display::printf("  | %-40s |\n", p.description);
    neo::display::printf("  +------------------------------------------+\n");

    if (p.dep_count > 0) {
        neo::display::printf("  | Dependencies:                            |\n");
        for (int d = 0; d < p.dep_count; d++) {
            int di = find_package(p.deps[d]);
            const char* status = (di >= 0 && repo[di].installed) ? "[OK]" : "[MISSING]";
            neo::display::printf("  |   %-24s %12s |\n", p.deps[d], status);
        }
        neo::display::printf("  +------------------------------------------+\n");
    }
    wait_key();
}

static void cmd_install(const char* name) {
    draw_header();
    int idx = find_package(name);
    neo::display::set_cursor(0, 3);

    if (idx < 0) {
        neo::display::set_fg(12);
        neo::display::printf("  Package '%s' not found in repository.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    if (repo[idx].installed) {
        neo::display::set_fg(14);
        neo::display::printf("  Package '%s' is already installed.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Resolve dependencies
    if (!resolve_deps(idx)) {
        neo::display::set_fg(12);
        neo::display::printf("  Failed to resolve dependencies for '%s'.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Show install plan
    neo::display::set_color(15, 0);
    neo::display::printf("  Install Plan:\n\n");
    neo::display::set_color(7, 0);

    unsigned long total_size = 0;
    for (int i = 0; i < install_order_count; i++) {
        int pi = install_order[i];
        neo::display::printf("    %d. %s (%s) - %lu KB\n",
            i + 1, repo[pi].name, repo[pi].version, repo[pi].size_kb);
        total_size += repo[pi].size_kb;
    }

    neo::display::printf("\n  Total download size: %lu KB\n", total_size);
    neo::display::printf("  Proceed? [Y/N] ");

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
            if (ch == 'y' || ch == 'Y') break;
            if (ch == 'n' || ch == 'N') return;
        }
        neo::proc::yield();
    }

    // Install each package
    for (int i = 0; i < install_order_count; i++) {
        int pi = install_order[i];
        animate_install(repo[pi].name, repo[pi].size_kb);
        repo[pi].installed = true;
        neo::timer::delay_ms(300);
        // Clear animation area
        for (int row = ANIM_ROW - 2; row <= ANIM_ROW + 3; row++) {
            neo::display::set_cursor(0, row);
            neo::display::clear_eol();
        }
    }

    neo::display::set_cursor(2, ANIM_ROW);
    neo::display::set_fg(10);
    neo::display::printf("All packages installed successfully!\n");
    neo::display::set_fg(7);
    wait_key();
}

static void cmd_remove(const char* name) {
    draw_header();
    int idx = find_package(name);
    neo::display::set_cursor(0, 3);

    if (idx < 0) {
        neo::display::set_fg(12);
        neo::display::printf("  Package '%s' not found.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    if (!repo[idx].installed) {
        neo::display::set_fg(14);
        neo::display::printf("  Package '%s' is not installed.\n", name);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Check reverse dependencies
    bool needed = false;
    for (int i = 0; i < repo_count; i++) {
        if (!repo[i].installed || i == idx) continue;
        for (int d = 0; d < repo[i].dep_count; d++) {
            if (neo_strcmp(repo[i].deps[d], name) == 0) {
                neo::display::set_fg(12);
                neo::display::printf("  Cannot remove: '%s' depends on '%s'\n", repo[i].name, name);
                neo::display::set_fg(7);
                needed = true;
            }
        }
    }

    if (needed) {
        wait_key();
        return;
    }

    neo::display::printf("  Remove %s? [Y/N] ", name);

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
            if (ch == 'y' || ch == 'Y') break;
            if (ch == 'n' || ch == 'N') return;
        }
        neo::proc::yield();
    }

    animate_remove(name);
    repo[idx].installed = false;
    neo::timer::delay_ms(300);
    wait_key();
}

static void cmd_update() {
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Checking for updates...\n\n");
    neo::display::set_color(7, 0);

    // Simulate update check with animation
    int dots = 50;
    for (int i = 0; i <= dots; i++) {
        pacman_frame++;
        draw_pacman_line(dots, i);
        neo::timer::delay_ms(30);
    }

    neo::display::set_cursor(0, ANIM_ROW + 2);
    neo::display::set_fg(10);
    neo::display::printf("  Repository synchronized. All packages up to date!\n");
    neo::display::set_fg(7);
    neo::display::printf("  %d packages available, ", repo_count);

    int inst = 0;
    for (int i = 0; i < repo_count; i++) if (repo[i].installed) inst++;
    neo::display::printf("%d installed.\n", inst);
    wait_key();
}

static void cmd_available() {
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Available Packages (not installed):\n\n");
    neo::display::set_color(7, 0);

    int count = 0;
    for (int i = 0; i < repo_count; i++) {
        if (!repo[i].installed) {
            neo::display::printf("  %-16s %-8s  %4lu KB  %s\n",
                repo[i].name, repo[i].version, repo[i].size_kb, repo[i].description);
            count++;
        }
    }

    if (count == 0) {
        neo::display::set_fg(10);
        neo::display::printf("  All packages are installed!\n");
        neo::display::set_fg(7);
    } else {
        neo::display::printf("\n  %d package(s) available for install.\n", count);
    }
    wait_key();
}

// --- Interactive shell ---
static void show_prompt() {
    neo::display::set_fg(14);
    neo::display::printf("neoman");
    neo::display::set_fg(7);
    neo::display::printf("> ");
}

static void show_help() {
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  NeoMan Commands:\n\n");
    neo::display::set_color(7, 0);
    neo::display::printf("  list                    List installed packages\n");
    neo::display::printf("  available               List packages not yet installed\n");
    neo::display::printf("  install <package>       Install a package\n");
    neo::display::printf("  remove <package>        Remove a package\n");
    neo::display::printf("  search <query>          Search packages\n");
    neo::display::printf("  info <package>          Show package details\n");
    neo::display::printf("  update                  Sync repository\n");
    neo::display::printf("  help                    Show this help\n");
    neo::display::printf("  quit                    Exit NeoMan\n");
    wait_key();
}

}  // namespace neoman

extern "C" void app_main(int argc, char** argv) {
    neoman::init_repo();

    // Command-line mode
    if (argc > 1) {
        if (neo_strcmp(argv[1], "list") == 0) { neoman::cmd_list(); return; }
        if (neo_strcmp(argv[1], "update") == 0) { neoman::cmd_update(); return; }
        if (neo_strcmp(argv[1], "available") == 0) { neoman::cmd_available(); return; }
        if (argc > 2) {
            if (neo_strcmp(argv[1], "install") == 0) { neoman::cmd_install(argv[2]); return; }
            if (neo_strcmp(argv[1], "remove") == 0) { neoman::cmd_remove(argv[2]); return; }
            if (neo_strcmp(argv[1], "search") == 0) { neoman::cmd_search(argv[2]); return; }
            if (neo_strcmp(argv[1], "info") == 0) { neoman::cmd_info(argv[2]); return; }
        }
    }

    // Interactive mode
    char line[128];
    neoman::draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::printf("  Welcome to NeoMan! Type 'help' for commands.\n\n");

    while (true) {
        neoman::show_prompt();
        neo::console::getline(line, sizeof(line), nullptr);

        // Parse command and argument
        char cmd[32] = {0};
        char arg[64] = {0};
        int i = 0, j = 0;

        // Skip leading spaces
        while (line[i] == ' ') i++;
        // Read command
        while (line[i] && line[i] != ' ' && j < 31) cmd[j++] = line[i++];
        cmd[j] = 0;
        // Skip spaces
        while (line[i] == ' ') i++;
        // Read argument
        j = 0;
        while (line[i] && j < 63) arg[j++] = line[i++];
        arg[j] = 0;

        if (neo_strcmp(cmd, "quit") == 0 || neo_strcmp(cmd, "exit") == 0 || neo_strcmp(cmd, "q") == 0) break;
        if (neo_strcmp(cmd, "help") == 0) { neoman::show_help(); continue; }
        if (neo_strcmp(cmd, "list") == 0) { neoman::cmd_list(); continue; }
        if (neo_strcmp(cmd, "available") == 0) { neoman::cmd_available(); continue; }
        if (neo_strcmp(cmd, "update") == 0) { neoman::cmd_update(); continue; }
        if (neo_strcmp(cmd, "install") == 0 && arg[0]) { neoman::cmd_install(arg); continue; }
        if (neo_strcmp(cmd, "remove") == 0 && arg[0]) { neoman::cmd_remove(arg); continue; }
        if (neo_strcmp(cmd, "search") == 0 && arg[0]) { neoman::cmd_search(arg); continue; }
        if (neo_strcmp(cmd, "info") == 0 && arg[0]) { neoman::cmd_info(arg); continue; }

        if (cmd[0]) {
            neo::display::set_fg(12);
            neo::display::printf("Unknown command: %s\n", cmd);
            neo::display::set_fg(7);
        }
    }
}
