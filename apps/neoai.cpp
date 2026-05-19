#include "../include/neobench.h"
#include "../lib/string.h"

// NeoAI - Interactive Assistant / Help System
// Help topics, command suggestions, system diagnostics, chat interface,
// knowledge base, "did you mean?" suggestions

namespace {

constexpr int MAX_INPUT = 128;
constexpr int MAX_OUTPUT = INODE_SIZE;
constexpr int MAX_HISTORY = 40;
constexpr int MAX_TOPICS = 60;
constexpr int MAX_COMMANDS = 50;

struct HelpTopic {
    const char* keyword;
    const char* title;
    const char* text;
};

struct CommandInfo {
    const char* name;
    const char* description;
    const char* usage;
};

static const HelpTopic topics[] = {
    {"memory", "Memory Management", "NeoBench uses Chip RAM (for DMA/display) and Fast RAM.\nUse 'mem' command to view usage.\nAlloc with neo::mem::alloc(size)."},
    {"display", "Display System", "Text-mode console with 80x25 (or larger) character grid.\nColors: 16 foreground, 8 background.\nUse neo::display functions for output."},
    {"keyboard", "Keyboard Input", "Amiga keyboard via scancode interface.\nUse neo::keyboard::read_scancode() for raw input.\ntranslate() converts scancodes to ASCII."},
    {"filesystem", "Filesystem", "NBFS (NeoBench File System) and FFS (Amiga Fast File System) supported.\nPaths use device:path format (e.g., SYS:apps/).\nUse filesystem API for file operations."},
    {"process", "Process Management", "Cooperative multitasking kernel.\nProcesses have priority levels.\nUse neo::proc::create() to spawn tasks."},
    {"audio", "Audio System", "4-channel Amiga audio (Paula chip).\nUse neo::audio::play_tone(ch,freq,ms).\nInit audio first with neo::audio::init()."},
    {"serial", "Serial Port", "RS-232 serial communication.\nInit with neo::serial::init(baud).\nUse puts/putchar for output."},
    {"storage", "Storage Devices", "IDE and SCSI drive support.\nUse probe() to detect devices.\nBlock-level read/write available."},
    {"network", "Networking", "Zorro bus network cards supported.\nUse neo::network::probe_zorro() to detect."},
    {"cpu", "CPU Information", "M68K CPU detection: 68000-68060.\nFPU, MMU, cache detection.\nUse neo::cpu::detect() for info."},
    {"rtc", "Real-Time Clock", "Battery-backed clock.\nUse neo::rtc::read() to get date/time.\nCheck neo::rtc::is_present() first."},
    {"timer", "Timer System", "System tick counter and delays.\nneo::timer::get_ticks() for timestamps.\ndelay_ms() for waiting."},
    {"apps", "Applications", "Run apps from the shell: appname [args]\nAvailable apps include editors, games, tools.\nType 'help apps' for full list."},
    {"shell", "Shell Usage", "Built-in command shell.\nType commands at the prompt.\nUse Tab for completion, Up/Down for history."},
    {"boot", "Boot Process", "NeoBench boots from Kickstart ROM.\nDetects hardware, inits subsystems.\nThen starts the shell."},
    {"interrupts", "Interrupt System", "7 interrupt levels (Amiga architecture).\nVBlank (level 3) drives the system tick.\nUse neo::interrupts::get_stats() for info."},
};
constexpr int NUM_TOPICS = 16;

static const CommandInfo commands[] = {
    {"help",     "Display help information",           "help [topic]"},
    {"mem",      "Show memory usage",                  "mem"},
    {"ps",       "List running processes",             "ps"},
    {"ls",       "List directory contents",            "ls [path]"},
    {"cd",       "Change directory",                   "cd <path>"},
    {"cat",      "Display file contents",              "cat <file>"},
    {"cp",       "Copy a file",                        "cp <src> <dst>"},
    {"mv",       "Move/rename a file",                 "mv <src> <dst>"},
    {"rm",       "Delete a file",                      "rm <file>"},
    {"mkdir",    "Create directory",                   "mkdir <path>"},
    {"mount",    "Show mounted filesystems",           "mount"},
    {"df",       "Show disk free space",               "df"},
    {"clear",    "Clear the screen",                   "clear"},
    {"reboot",   "Reboot the system",                  "reboot"},
    {"uptime",   "Show system uptime",                 "uptime"},
    {"date",     "Show current date/time",             "date"},
    {"echo",     "Print text",                         "echo <text>"},
    {"ver",      "Show version info",                  "ver"},
    {"cpuinfo",  "Show CPU information",               "cpuinfo"},
    {"neocalc",  "Calculator app",                     "neocalc"},
    {"neoedit",  "Text editor",                        "neoedit [file]"},
    {"neopaint", "ASCII art paint",                    "neopaint"},
    {"snake",    "Snake game",                         "snake"},
    {"tetris",   "Tetris game",                        "tetris"},
    {"chess",    "Chess game",                         "chess"},
    {"filemanager","File manager",                     "filemanager"},
    {"sysmonitor","System monitor",                    "sysmonitor"},
    {"neoclock", "Clock/world time",                   "neoclock"},
    {"settings", "System settings",                    "settings"},
    {"neoinstall","HD installer",                      "neoinstall"},
};
constexpr int NUM_COMMANDS = 30;

struct ChatLine {
    char text[MAX_OUTPUT];
    bool is_user;  // true = user input, false = AI response
};

struct AIState {
    bool running;
    char input[MAX_INPUT];
    int input_len;
    ChatLine history[MAX_HISTORY];
    int hist_count;
    int scroll;
};

static AIState ai;

// String utility - to lowercase
static void to_lower(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = (src[i] >= 'A' && src[i] <= 'Z') ? src[i] + 32 : src[i];
        i++;
    }
    dst[i] = 0;
}

// Simple Levenshtein distance for "did you mean?"
static int edit_distance(const char* a, const char* b) {
    int la = neo_strlen(a);
    int lb = neo_strlen(b);
    if (la > 16 || lb > 16) return 99;

    // Simple DP - use stack array
    int dp[17][17];
    for (int i = 0; i <= la; i++) dp[i][0] = i;
    for (int j = 0; j <= lb; j++) dp[0][j] = j;
    for (int i = 1; i <= la; i++) {
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            dp[i][j] = dp[i-1][j] + 1;
            if (dp[i][j-1] + 1 < dp[i][j]) dp[i][j] = dp[i][j-1] + 1;
            if (dp[i-1][j-1] + cost < dp[i][j]) dp[i][j] = dp[i-1][j-1] + cost;
        }
    }
    return dp[la][lb];
}

static void add_response(const char* text) {
    if (ai.hist_count < MAX_HISTORY) {
        neo_strncpy(ai.history[ai.hist_count].text, text, MAX_OUTPUT - 1);
        ai.history[ai.hist_count].is_user = false;
        ai.hist_count++;
    }
}

static void add_user_line(const char* text) {
    if (ai.hist_count < MAX_HISTORY) {
        neo_strncpy(ai.history[ai.hist_count].text, text, MAX_OUTPUT - 1);
        ai.history[ai.hist_count].is_user = true;
        ai.hist_count++;
    }
}

static bool str_contains(const char* haystack, const char* needle) {
    int hlen = neo_strlen(haystack);
    int nlen = neo_strlen(needle);
    for (int i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (int j = 0; j < nlen; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

static void process_input() {
    if (ai.input_len == 0) return;

    char lower[MAX_INPUT];
    to_lower(lower, ai.input, MAX_INPUT);
    add_user_line(ai.input);

    // Check for special commands
    if (neo_strcmp(lower, "quit") == 0 || neo_strcmp(lower, "exit") == 0) {
        add_response("Goodbye! Type 'exit' at shell to return.");
        ai.running = false;
        return;
    }

    if (neo_strcmp(lower, "help") == 0) {
        add_response("I can help with: memory, display, keyboard, filesystem,");
        add_response("process, audio, serial, storage, network, cpu, rtc, timer,");
        add_response("apps, shell, boot, interrupts.");
        add_response("Try: 'help <topic>', 'diag', 'commands', or ask a question!");
        ai.input_len = 0;
        ai.input[0] = 0;
        return;
    }

    // Diagnostics
    if (str_contains(lower, "diag") || str_contains(lower, "diagnos") || str_contains(lower, "check")) {
        add_response("Running system diagnostics...");

        // Memory
        unsigned int free_mem = neo::mem::get_free_mem();
        unsigned int total = neo::mem::get_total_mem();
        unsigned int chip = neo::mem::get_free_chip();
        unsigned int fast = neo::mem::get_free_fast();
        char buf[MAX_OUTPUT];
        ksprintf(buf, MAX_OUTPUT, "  Memory: %u/%u bytes free (Chip:%u Fast:%u)", free_mem, total, chip, fast);
        add_response(buf);

        int pct = (total > 0) ? (int)((unsigned long long)free_mem * 100 / total) : 0;
        if (pct < 20) add_response("  WARNING: Low memory! Close unused apps.");
        else add_response("  Memory status: OK");

        // CPU
        neo::cpu::CpuInfo cpu;
        neo::cpu::detect(cpu);
        ksprintf(buf, MAX_OUTPUT, "  CPU: Type %d, FPU: %d, Clock: %dMHz", cpu.type, cpu.fpu_type, cpu.clock_mhz);
        add_response(buf);

        // Uptime
        unsigned int up = neo::timer::get_uptime_seconds();
        ksprintf(buf, MAX_OUTPUT, "  Uptime: %d hours %d minutes", up / 3600, (up / 60) % 60);
        add_response(buf);

        // RTC
        if (neo::rtc::is_present()) {
            neo::rtc::DateTime dt;
            neo::rtc::read(dt);
            ksprintf(buf, MAX_OUTPUT, "  RTC: %d/%d/%d %02d:%02d:%02d", dt.month, dt.day, dt.year, dt.hour, dt.minute, dt.second);
            add_response(buf);
        } else {
            add_response("  RTC: Not detected");
        }

        // Interrupts
        neo::interrupts::Stats istats;
        neo::interrupts::get_stats(istats);
        ksprintf(buf, MAX_OUTPUT, "  Interrupts: %u total, %u spurious", istats.total, istats.spurious);
        add_response(buf);

        add_response("Diagnostics complete.");
        ai.input_len = 0;
        ai.input[0] = 0;
        return;
    }

    // Command list
    if (str_contains(lower, "command") || str_contains(lower, "list")) {
        add_response("Available commands:");
        for (int i = 0; i < NUM_COMMANDS; i++) {
            char buf[MAX_OUTPUT];
            ksprintf(buf, MAX_OUTPUT, "  %-14s - %s", commands[i].name, commands[i].description);
            add_response(buf);
        }
        ai.input_len = 0;
        ai.input[0] = 0;
        return;
    }

    // Help topic search
    bool found_topic = false;
    for (int i = 0; i < NUM_TOPICS; i++) {
        if (str_contains(lower, topics[i].keyword)) {
            char buf[MAX_OUTPUT];
            ksprintf(buf, MAX_OUTPUT, "[%s]", topics[i].title);
            add_response(buf);
            // Split text by newlines
            const char* p = topics[i].text;
            char line[MAX_OUTPUT];
            int li = 0;
            while (*p) {
                if (*p == '\n') {
                    line[li] = 0;
                    add_response(line);
                    li = 0;
                } else {
                    if (li < MAX_OUTPUT - 1) line[li++] = *p;
                }
                p++;
            }
            if (li > 0) { line[li] = 0; add_response(line); }
            found_topic = true;
            break;
        }
    }

    if (!found_topic) {
        // Command suggestion
        bool found_cmd = false;
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (str_contains(lower, commands[i].name)) {
                char buf[MAX_OUTPUT];
                ksprintf(buf, MAX_OUTPUT, "Command: %s", commands[i].name);
                add_response(buf);
                ksprintf(buf, MAX_OUTPUT, "  %s", commands[i].description);
                add_response(buf);
                ksprintf(buf, MAX_OUTPUT, "  Usage: %s", commands[i].usage);
                add_response(buf);
                found_cmd = true;
                break;
            }
        }

        if (!found_cmd) {
            // "Did you mean?" - find closest command
            int best_dist = 99;
            int best_idx = -1;
            // Extract first word
            char first_word[32];
            int wi = 0;
            while (lower[wi] && lower[wi] != ' ' && wi < 31) { first_word[wi] = lower[wi]; wi++; }
            first_word[wi] = 0;

            for (int i = 0; i < NUM_COMMANDS; i++) {
                int d = edit_distance(first_word, commands[i].name);
                if (d < best_dist) { best_dist = d; best_idx = i; }
            }

            if (best_dist <= 3 && best_idx >= 0) {
                char buf[MAX_OUTPUT];
                ksprintf(buf, MAX_OUTPUT, "Did you mean '%s'? (%s)", commands[best_idx].name, commands[best_idx].description);
                add_response(buf);
            } else {
                add_response("I don't understand that. Try 'help' for available topics,");
                add_response("'commands' for command list, or 'diag' for diagnostics.");
            }
        }
    }

    ai.input_len = 0;
    ai.input[0] = 0;
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_color(15, 4);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("NeoAI Assistant");
    neo::display::set_bold(false);
    neo::display::set_cursor(w - 30, 0);
    neo::display::puts("help | diag | commands | exit");
    neo::display::set_color(7, 0);

    // Chat area
    int chat_start = 2;
    int chat_end = h - 4;
    int avail = chat_end - chat_start;

    int start_idx = ai.hist_count > avail ? ai.hist_count - avail : 0;
    int row = chat_start;
    for (int i = start_idx; i < ai.hist_count && row < chat_end; i++) {
        neo::display::set_cursor(0, row);
        if (ai.history[i].is_user) {
            neo::display::set_color(11, 0);
            neo::display::puts(" You> ");
            neo::display::set_color(15, 0);
        } else {
            neo::display::set_color(10, 0);
            neo::display::puts("  AI> ");
            neo::display::set_color(7, 0);
        }
        // Truncate if too long for screen
        int max_chars = w - 8;
        int len = neo_strlen(ai.history[i].text);
        if (len > max_chars) {
            char tmp[INODE_SIZE];
            neo_strncpy(tmp, ai.history[i].text, max_chars);
            tmp[max_chars] = 0;
            neo::display::puts(tmp);
        } else {
            neo::display::puts(ai.history[i].text);
        }
        row++;
    }

    // Separator
    neo::display::set_cursor(0, h - 3);
    neo::display::set_color(8, 0);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::set_color(7, 0);

    // Input line
    neo::display::set_cursor(0, h - 2);
    neo::display::set_color(11, 0);
    neo::display::puts(" > ");
    neo::display::set_color(15, 0);
    neo::display::puts(ai.input);
    neo::display::putchar('_');
    neo::display::set_color(7, 0);

    // Status
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("Enter=Send  Esc=Quit  Type a question or command");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(sc, shift);

    if (sc == 0x01) { ai.running = false; return; }

    if (ch == '\r' || ch == '\n' || sc == 0x44) {
        process_input();
        return;
    }

    if (sc == 0x41 || ch == 8) {
        if (ai.input_len > 0) {
            ai.input[--ai.input_len] = 0;
        }
        return;
    }

    if (ch >= 32 && ch < 127 && ai.input_len < MAX_INPUT - 1) {
        ai.input[ai.input_len++] = ch;
        ai.input[ai.input_len] = 0;
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&ai, 0, sizeof(ai));
    ai.running = true;

    add_response("Welcome to NeoAI Assistant!");
    add_response("I can help you with NeoBench commands, system info, and troubleshooting.");
    add_response("Try: 'help', 'diag', 'commands', or ask about any topic.");
    add_response("");

    draw_ui();

    while (ai.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_ui();
        }
        neo::timer::delay_ms(15);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("NeoAI exited.\n");
}
