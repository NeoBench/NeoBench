#include "../include/neobench.h"
#include "../lib/string.h"

// NeoHelp - Documentation Browser
// Hyperlink support, page navigation, table of contents, built-in help

namespace neohelp {

static const int MAX_LINES = 512;
static const int MAX_LINE_LEN = 120;
static const int MAX_LINKS = 64;
static const int MAX_TOPICS = 32;

struct HelpLine {
    char text[MAX_LINE_LEN];
    bool is_heading;
    bool is_separator;
};

struct HelpLink {
    int line;
    int col;
    int len;
    char target[64];
};

struct HelpTopic {
    char name[64];
    char title[80];
    int line_count;
    HelpLine* lines;     // pointer into shared buffer
    int link_count;
    HelpLink links[16];  // max links per topic
};

static HelpLine line_buf[MAX_LINES];
static int total_lines = 0;
static HelpTopic topics[MAX_TOPICS];
static int topic_count = 0;
static int current_topic = 0;
static int scroll_pos = 0;
static int selected_link = -1;

// --- Built-in help content ---
static int add_line(const char* text, bool heading, bool sep) {
    if (total_lines >= MAX_LINES) return -1;
    HelpLine& l = line_buf[total_lines];
    neo_strncpy(l.text, text, MAX_LINE_LEN - 1);
    l.text[MAX_LINE_LEN - 1] = 0;
    l.is_heading = heading;
    l.is_separator = sep;
    return total_lines++;
}

static void add_topic(const char* name, const char* title) {
    if (topic_count >= MAX_TOPICS) return;
    HelpTopic& t = topics[topic_count];
    neo_strcpy(t.name, name);
    neo_strcpy(t.title, title);
    t.lines = &line_buf[total_lines];
    t.line_count = 0;
    t.link_count = 0;
}

static void topic_line(const char* text, bool heading = false) {
    if (topic_count == 0) return;
    HelpTopic& t = topics[topic_count - 1];
    int idx = add_line(text, heading, false);
    if (idx >= 0) t.line_count++;
}

static void topic_sep() {
    if (topic_count == 0) return;
    HelpTopic& t = topics[topic_count - 1];
    int idx = add_line("", false, true);
    if (idx >= 0) t.line_count++;
}

static void topic_link_line(const char* text, const char* target) {
    if (topic_count == 0) return;
    HelpTopic& t = topics[topic_count - 1];
    int idx = add_line(text, false, false);
    if (idx >= 0) {
        t.line_count++;
        if (t.link_count < 16) {
            HelpLink& lk = t.links[t.link_count++];
            lk.line = t.line_count - 1;
            lk.col = 2;
            lk.len = neo_strlen(text);
            neo_strcpy(lk.target, target);
        }
    }
}

static void finish_topic() {
    topic_count++;
}

static void build_help_db() {
    total_lines = 0;
    topic_count = 0;

    // --- Index ---
    add_topic("index", "NeoBench Help - Table of Contents");
    topic_line("NeoBench Help System", true);
    topic_sep();
    topic_line("Welcome to the NeoBench documentation browser.");
    topic_line("Navigate using Page Up/Down, follow links with Enter.");
    topic_sep();
    topic_line("System Overview:", true);
    topic_link_line("  [>] Getting Started", "getting_started");
    topic_link_line("  [>] Shell Commands", "shell");
    topic_link_line("  [>] System Architecture", "architecture");
    topic_sep();
    topic_line("Applications:", true);
    topic_link_line("  [>] NBench - Benchmarking Suite", "nbench");
    topic_link_line("  [>] NeoMan - Package Manager", "neoman");
    topic_link_line("  [>] DiskTools - Disk Management", "disktools");
    topic_link_line("  [>] NeoTask - Task Manager", "neotask");
    topic_link_line("  [>] NeoFind - File Search", "neofind");
    topic_link_line("  [>] NeoZip - Archive Manager", "neozip");
    topic_link_line("  [>] NeoTheme - Theme Engine", "neotheme");
    topic_link_line("  [>] NeoHelp - This Help Browser", "neohelp_self");
    topic_link_line("  [>] NeoScript - Scripting Language", "neoscript");
    topic_sep();
    topic_line("Programming:", true);
    topic_link_line("  [>] Kernel API Reference", "api");
    topic_link_line("  [>] Writing Applications", "writing_apps");
    finish_topic();

    // --- Getting Started ---
    add_topic("getting_started", "Getting Started with NeoBench");
    topic_line("Getting Started", true);
    topic_sep();
    topic_line("NeoBench is a bare-metal Amiga kernel designed for");
    topic_line("benchmarking and educational purposes. It runs directly");
    topic_line("on 68000-series hardware without AmigaOS.");
    topic_line("");
    topic_line("First Steps:", true);
    topic_line("  1. The shell starts automatically on boot");
    topic_line("  2. Type 'help' to see available commands");
    topic_line("  3. Type 'ls' to list files");
    topic_line("  4. Run apps by typing their name (e.g. 'nbench')");
    topic_line("");
    topic_line("System Requirements:", true);
    topic_line("  - Amiga with 68000 or better CPU");
    topic_line("  - 512 KB Chip RAM minimum");
    topic_line("  - Hard drive or CF card recommended");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- Shell ---
    add_topic("shell", "Shell Commands Reference");
    topic_line("Shell Commands", true);
    topic_sep();
    topic_line("Navigation:", true);
    topic_line("  ls [path]        List directory contents");
    topic_line("  cd <path>        Change directory");
    topic_line("  pwd              Print working directory");
    topic_line("  cat <file>       Display file contents");
    topic_line("");
    topic_line("File Operations:", true);
    topic_line("  cp <src> <dst>   Copy file");
    topic_line("  mv <src> <dst>   Move/rename file");
    topic_line("  rm <file>        Remove file");
    topic_line("  mkdir <dir>      Create directory");
    topic_line("");
    topic_line("System:", true);
    topic_line("  ps               List processes");
    topic_line("  mem              Memory information");
    topic_line("  sysinfo          System information");
    topic_line("  uptime           Show uptime");
    topic_line("  reboot           Reboot system");
    topic_line("  clear            Clear screen");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- Architecture ---
    add_topic("architecture", "System Architecture");
    topic_line("System Architecture", true);
    topic_sep();
    topic_line("NeoBench Kernel Components:", true);
    topic_line("");
    topic_line("  +------------------------------------------+");
    topic_line("  |          Applications (apps/)            |");
    topic_line("  +------------------------------------------+");
    topic_line("  |         Shell / Console Interface         |");
    topic_line("  +------------------------------------------+");
    topic_line("  |  Process  | Memory  | Filesystem | Timer  |");
    topic_line("  |  Manager  | Manager | (NBFS/FFS) | System |");
    topic_line("  +------------------------------------------+");
    topic_line("  |  Keyboard | Display | Audio | Storage    |");
    topic_line("  |  Driver   | Driver  | Paula | IDE/SCSI   |");
    topic_line("  +------------------------------------------+");
    topic_line("  |      Hardware Abstraction Layer           |");
    topic_line("  +------------------------------------------+");
    topic_line("  |    Amiga Custom Chips / 680x0 CPU         |");
    topic_line("  +------------------------------------------+");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- NBench ---
    add_topic("nbench", "NBench - Benchmarking Suite");
    topic_line("NBench - Benchmarking Suite", true);
    topic_sep();
    topic_line("Usage: nbench [--all]");
    topic_line("");
    topic_line("NBench provides comprehensive system benchmarking:");
    topic_line("");
    topic_line("Tests:", true);
    topic_line("  CPU Integer    Dhrystone-like integer operations");
    topic_line("  FPU Mandelbrot Fixed-point Mandelbrot set computation");
    topic_line("  Memory BW      Memory copy and fill bandwidth");
    topic_line("  Disk I/O       Sequential block read throughput");
    topic_line("  Chip RAM BW    Chip RAM bandwidth test");
    topic_line("");
    topic_line("Options:", true);
    topic_line("  --all, -a      Run all tests automatically");
    topic_line("");
    topic_line("The interactive menu lets you run individual tests");
    topic_line("and view detailed results with score comparisons.");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- NeoMan ---
    add_topic("neoman", "NeoMan - Package Manager");
    topic_line("NeoMan - Package Manager (Pac-Man Theme!)", true);
    topic_sep();
    topic_line("Usage: neoman [command] [package]");
    topic_line("");
    topic_line("Commands:", true);
    topic_line("  list             Show installed packages");
    topic_line("  available        Show packages not installed");
    topic_line("  install <pkg>    Install a package");
    topic_line("  remove <pkg>     Remove a package");
    topic_line("  search <query>   Search packages");
    topic_line("  info <pkg>       Package details");
    topic_line("  update           Sync repository");
    topic_line("");
    topic_line("Features:", true);
    topic_line("  - Pac-Man eating animation during install");
    topic_line("  - Dependency resolution with topological sort");
    topic_line("  - Reverse dependency checking on remove");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- Short entries for other apps ---
    const char* app_names[] = {"disktools", "neotask", "neofind", "neozip", "neotheme", "neohelp_self", "neoscript"};
    const char* app_titles[] = {
        "DiskTools - Disk Management", "NeoTask - Task Manager",
        "NeoFind - File Search", "NeoZip - Archive Manager",
        "NeoTheme - Theme Engine", "NeoHelp - Documentation Browser",
        "NeoScript - Scripting Language"
    };
    const char* app_descs[][6] = {
        {"Partition editor, NBFS formatter, disk usage,", "disk cloner. Interactive menu-driven UI.", "Usage: disktools", "", "", ""},
        {"Real-time process list with CPU/memory usage.", "Kill processes, change priority, CPU history.", "Usage: neotask", "", "", ""},
        {"Recursive file search with * and ? wildcards.", "Filter by name, size, type. Multiple paths.", "Usage: neofind <pattern> [path]", "", "", ""},
        {"Archive manager with RLE compression.", "Create, list, and extract .nza archives.", "Usage: neozip [list|extract|create] <file>", "", "", ""},
        {"Color scheme editor with real-time preview.", "Built-in themes: Default, Dark, Light, Retro, Amber.", "Usage: neotheme [load <file>]", "", "", ""},
        {"This help browser! Navigate with Page Up/Down.", "Follow links with Enter, go back with Backspace.", "Usage: neohelp [topic]", "", "", ""},
        {"Simple scripting language interpreter.", "Variables, loops, functions, file I/O.", "Usage: neoscript [script.ns]", "", "", ""},
    };

    for (int a = 0; a < 7; a++) {
        add_topic(app_names[a], app_titles[a]);
        topic_line(app_titles[a], true);
        topic_sep();
        for (int d = 0; d < 6; d++) {
            if (app_descs[a][d][0]) topic_line(app_descs[a][d]);
        }
        topic_sep();
        topic_link_line("  [<] Back to Index", "index");
        finish_topic();
    }

    // --- API Reference ---
    add_topic("api", "Kernel API Reference");
    topic_line("Kernel API Reference", true);
    topic_sep();
    topic_line("Display:", true);
    topic_line("  neo::display::putchar(c)       Print character");
    topic_line("  neo::display::puts(str)        Print string");
    topic_line("  neo::display::printf(fmt,...)  Formatted print");
    topic_line("  neo::display::clear()          Clear screen");
    topic_line("  neo::display::set_color(fg,bg) Set colors");
    topic_line("  neo::display::set_cursor(x,y)  Position cursor");
    topic_line("  neo::display::get_width()      Screen width");
    topic_line("  neo::display::get_height()     Screen height");
    topic_line("");
    topic_line("Memory:", true);
    topic_line("  neo::mem::alloc(size)          Allocate memory");
    topic_line("  neo::mem::free(ptr)            Free memory");
    topic_line("  neo::mem::alloc_chip(size)     Chip RAM alloc");
    topic_line("  neo::mem::get_free_mem()       Free memory");
    topic_line("");
    topic_line("Keyboard:", true);
    topic_line("  neo::keyboard::key_available() Check for key");
    topic_line("  neo::keyboard::read_scancode() Read scancode");
    topic_line("  neo::keyboard::translate(sc,s) To ASCII");
    topic_line("");
    topic_line("Filesystem:", true);
    topic_line("  neo::filesystem::open(fh,p,m)  Open file");
    topic_line("  neo::filesystem::read(fh,b,s)  Read data");
    topic_line("  neo::filesystem::write(fh,b,s) Write data");
    topic_line("  neo::filesystem::close(fh)     Close file");
    topic_line("  neo::filesystem::readdir(p,e,m) List directory");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();

    // --- Writing Apps ---
    add_topic("writing_apps", "Writing Applications");
    topic_line("Writing NeoBench Applications", true);
    topic_sep();
    topic_line("Every app needs:");
    topic_line("  #include \"../include/neobench.h\"");
    topic_line("");
    topic_line("  extern \"C\" void app_main(int argc, char** argv)");
    topic_line("  {");
    topic_line("      // Your code here");
    topic_line("  }");
    topic_line("");
    topic_line("Rules:", true);
    topic_line("  - No stdlib, stdio, iostream, or STL");
    topic_line("  - Use kprintf() instead of printf()");
    topic_line("  - Use neo::mem::alloc() instead of malloc/new");
    topic_line("  - No exceptions, no RTTI, no virtual functions");
    topic_line("  - Use neo::display::* for all screen output");
    topic_line("  - All drawing is character-based (text mode)");
    topic_sep();
    topic_link_line("  [<] Back to Index", "index");
    finish_topic();
}

// --- Navigation ---
static int find_topic(const char* name) {
    for (int i = 0; i < topic_count; i++) {
        if (neo_strcmp(topics[i].name, name) == 0) return i;
    }
    return -1;
}

static void navigate_to(const char* target) {
    int idx = find_topic(target);
    if (idx >= 0) {
        current_topic = idx;
        scroll_pos = 0;
        selected_link = topics[idx].link_count > 0 ? 0 : -1;
    }
}

// --- Back stack ---
static const int MAX_HISTORY = 16;
static int history_stack[MAX_HISTORY];
static int history_top = 0;

static void push_history() {
    if (history_top < MAX_HISTORY) {
        history_stack[history_top++] = current_topic;
    }
}

static bool pop_history() {
    if (history_top > 0) {
        current_topic = history_stack[--history_top];
        scroll_pos = 0;
        selected_link = topics[current_topic].link_count > 0 ? 0 : -1;
        return true;
    }
    return false;
}

// --- Display ---
static void draw_page() {
    neo::display::clear();

    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_cursor(0, 0);
    neo::display::set_color(14, 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NeoHelp - %s", topics[current_topic].title);
    neo::display::set_color(7, 0);

    // Separator
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('=');

    HelpTopic& t = topics[current_topic];
    int visible = h - 4;  // Title + sep + footer
    int max_scroll = t.line_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_pos > max_scroll) scroll_pos = max_scroll;

    for (int row = 0; row < visible && (row + scroll_pos) < t.line_count; row++) {
        int li = row + scroll_pos;
        HelpLine& line = t.lines[li];

        neo::display::set_cursor(0, 2 + row);

        if (line.is_separator) {
            neo::display::set_fg(8);
            for (int i = 0; i < w; i++) neo::display::putchar('-');
            neo::display::set_fg(7);
            continue;
        }

        if (line.is_heading) {
            neo::display::set_bold(true);
            neo::display::set_fg(15);
            neo::display::printf("  %s", line.text);
            neo::display::set_bold(false);
            neo::display::set_fg(7);
            continue;
        }

        // Check if this line is a link
        bool is_link = false;
        int link_idx = -1;
        for (int lk = 0; lk < t.link_count; lk++) {
            if (t.links[lk].line == li) {
                is_link = true;
                link_idx = lk;
                break;
            }
        }

        if (is_link) {
            if (link_idx == selected_link) {
                neo::display::set_color(0, 11);
            } else {
                neo::display::set_fg(11);  // Cyan for links
            }
            neo::display::printf("  %s", line.text);
            neo::display::set_color(7, 0);
        } else {
            neo::display::printf("  %s", line.text);
        }
    }

    // Scrollbar indicator
    if (t.line_count > visible) {
        int sb_h = visible;
        int thumb_pos = (scroll_pos * sb_h) / t.line_count;
        int thumb_size = (visible * sb_h) / t.line_count;
        if (thumb_size < 1) thumb_size = 1;

        for (int i = 0; i < sb_h; i++) {
            neo::display::set_cursor(w - 1, 2 + i);
            if (i >= thumb_pos && i < thumb_pos + thumb_size) {
                neo::display::set_fg(15);
                neo::display::putchar('#');
            } else {
                neo::display::set_fg(8);
                neo::display::putchar('|');
            }
        }
        neo::display::set_fg(7);
    }

    // Footer
    neo::display::set_cursor(0, h - 2);
    neo::display::set_fg(8);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::set_cursor(0, h - 1);
    neo::display::set_fg(14);
    neo::display::printf(" [PgUp/PgDn] Scroll  [Tab] Next link  [Enter] Follow  [Bksp] Back  [Q] Quit");

    int pct = t.line_count > 0 ? ((scroll_pos + visible) * 100) / t.line_count : 100;
    if (pct > 100) pct = 100;
    neo::display::set_cursor(w - 8, h - 1);
    neo::display::printf(" %3d%% ", pct);
    neo::display::set_fg(7);
}

}  // namespace neohelp

extern "C" void app_main(int argc, char** argv) {
    neohelp::build_help_db();
    neohelp::history_top = 0;

    // Navigate to initial topic
    if (argc > 1) {
        neohelp::navigate_to(argv[1]);
    } else {
        neohelp::navigate_to("index");
    }

    neohelp::draw_page();

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            int h = neo::display::get_height();
            int page_size = h - 4;
            neohelp::HelpTopic& t = neohelp::topics[neohelp::current_topic];

            switch (ch) {
                case 'q': case 'Q':
                    return;

                case 'j': case 'J':  // Scroll down
                    neohelp::scroll_pos++;
                    neohelp::draw_page();
                    break;

                case 'k': case 'K':  // Scroll up
                    if (neohelp::scroll_pos > 0) neohelp::scroll_pos--;
                    neohelp::draw_page();
                    break;

                case 'f': case 'F':  // Page down
                case ' ':
                    neohelp::scroll_pos += page_size;
                    neohelp::draw_page();
                    break;

                case 'b': case 'B':  // Page up
                    neohelp::scroll_pos -= page_size;
                    if (neohelp::scroll_pos < 0) neohelp::scroll_pos = 0;
                    neohelp::draw_page();
                    break;

                case '\t':  // Tab - next link
                    if (t.link_count > 0) {
                        neohelp::selected_link = (neohelp::selected_link + 1) % t.link_count;
                        neohelp::draw_page();
                    }
                    break;

                case '\n': case '\r':  // Enter - follow link
                    if (neohelp::selected_link >= 0 && neohelp::selected_link < t.link_count) {
                        neohelp::push_history();
                        neohelp::navigate_to(t.links[neohelp::selected_link].target);
                        neohelp::draw_page();
                    }
                    break;

                case 8:  // Backspace - go back
                    if (neohelp::pop_history()) {
                        neohelp::draw_page();
                    }
                    break;

                case 'h': case 'H':  // Home
                    neohelp::push_history();
                    neohelp::navigate_to("index");
                    neohelp::draw_page();
                    break;
            }
        }
        neo::proc::yield();
    }
}
