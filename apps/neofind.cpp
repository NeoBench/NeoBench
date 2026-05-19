#include "../include/neobench.h"
#include "../lib/string.h"

// NeoFind - File Search Utility
// Recursive directory search with wildcards, size filters, type filters

namespace neofind {

static const int MAX_RESULTS = INODE_SIZE;
static const int MAX_PATH_LEN = 512;
static const int MAX_SEARCH_PATHS = 8;

struct SearchResult {
    char path[MAX_PATH_LEN];
    unsigned long size;
    int type;  // 0=file, 1=dir
};

struct SearchCriteria {
    char pattern[64];          // Name pattern with * and ?
    unsigned long min_size;     // 0 = no min
    unsigned long max_size;     // 0 = no max
    int type_filter;           // -1=all, 0=file, 1=dir
    char search_paths[MAX_SEARCH_PATHS][MAX_PATH_LEN];
    int path_count;
    bool case_sensitive;
};

static SearchResult results[MAX_RESULTS];
static int result_count = 0;
static int files_scanned = 0;
static int dirs_scanned = 0;
static SearchCriteria criteria;

// --- Wildcard matching ---
static bool wildcard_match(const char* pattern, const char* str, bool case_sens) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == 0) return true;
            while (*str) {
                if (wildcard_match(pattern, str, case_sens)) return true;
                str++;
            }
            return wildcard_match(pattern, str, case_sens);
        }
        if (*pattern == '?') {
            pattern++;
            str++;
            continue;
        }

        char pc = *pattern;
        char sc = *str;
        if (!case_sens) {
            if (pc >= 'A' && pc <= 'Z') pc += 32;
            if (sc >= 'A' && sc <= 'Z') sc += 32;
        }

        if (pc != sc) return false;
        pattern++;
        str++;
    }

    while (*pattern == '*') pattern++;
    return (*pattern == 0 && *str == 0);
}

// --- Path concatenation ---
static void path_join(char* out, int max, const char* dir, const char* name) {
    int dlen = neo_strlen(dir);
    int nlen = neo_strlen(name);
    if (dlen + nlen + 2 >= max) {
        out[0] = 0;
        return;
    }
    neo_strcpy(out, dir);
    if (dlen > 0 && dir[dlen-1] != '/') {
        out[dlen] = '/';
        out[dlen+1] = 0;
    }
    neo_strcat(out, name);
}

// --- Recursive search ---
static void search_dir(const char* path, int depth) {
    if (depth > 10) return;  // Prevent infinite recursion
    if (result_count >= MAX_RESULTS) return;

    neo::filesystem::DirEntry entries[32];
    int count = neo::filesystem::readdir(path, entries, 32);
    dirs_scanned++;

    for (int i = 0; i < count && result_count < MAX_RESULTS; i++) {
        neo::filesystem::DirEntry& e = entries[i];
        files_scanned++;

        // Skip . and ..
        if (neo_strcmp(e.name, ".") == 0 || neo_strcmp(e.name, "..") == 0) continue;

        char full_path[MAX_PATH_LEN];
        path_join(full_path, MAX_PATH_LEN, path, e.name);

        // Check criteria
        bool matches = true;

        // Name pattern
        if (criteria.pattern[0]) {
            if (!wildcard_match(criteria.pattern, e.name, criteria.case_sensitive)) {
                matches = false;
            }
        }

        // Type filter
        if (criteria.type_filter >= 0 && e.type != criteria.type_filter) {
            matches = false;
        }

        // Size filter (files only)
        if (e.type == 0) {
            if (criteria.min_size > 0 && e.size < criteria.min_size) matches = false;
            if (criteria.max_size > 0 && e.size > criteria.max_size) matches = false;
        }

        if (matches) {
            SearchResult& r = results[result_count++];
            neo_strncpy(r.path, full_path, MAX_PATH_LEN - 1);
            r.path[MAX_PATH_LEN - 1] = 0;
            r.size = e.size;
            r.type = e.type;
        }

        // Recurse into directories
        if (e.type == 1) {
            search_dir(full_path, depth + 1);
        }
    }
}

// --- UI ---
static void draw_header() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NeoFind v1.0 - File Search Utility");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
}

static void format_size(char* buf, int max, unsigned long size) {
    if (size >= 1024 * 1024) {
        ksprintf(buf, max, "%lu MB", size / (1024 * 1024));
    } else if (size >= 1024) {
        ksprintf(buf, max, "%lu KB", size / 1024);
    } else {
        ksprintf(buf, max, "%lu B", size);
    }
}

static void display_results(int page) {
    int h = neo::display::get_height();
    int per_page = h - 8;
    int pages = (result_count + per_page - 1) / per_page;
    if (pages == 0) pages = 1;
    if (page >= pages) page = pages - 1;

    draw_header();

    neo::display::set_cursor(0, 2);
    neo::display::set_color(15, 0);
    neo::display::printf("  Search: \"%s\"  |  Found: %d  |  Scanned: %d files, %d dirs  |  Page %d/%d\n",
        criteria.pattern, result_count, files_scanned, dirs_scanned, page + 1, pages);
    neo::display::set_color(7, 0);

    neo::display::printf("  +-----+------+------------+--------------------------------------------------+\n");
    neo::display::printf("  |  #  | Type | Size       | Path                                             |\n");
    neo::display::printf("  +-----+------+------------+--------------------------------------------------+\n");

    int start = page * per_page;
    int end = start + per_page;
    if (end > result_count) end = result_count;

    for (int i = start; i < end; i++) {
        SearchResult& r = results[i];
        char size_str[16];
        format_size(size_str, 16, r.size);

        if (r.type == 1) {
            neo::display::set_fg(11);  // Cyan for dirs
            neo::display::printf("  | %3d | DIR  | %10s | %-48s |\n",
                i + 1, size_str, r.path);
        } else {
            neo::display::set_fg(7);
            neo::display::printf("  | %3d | FILE | %10s | %-48s |\n",
                i + 1, size_str, r.path);
        }
    }
    neo::display::set_fg(7);
    neo::display::printf("  +-----+------+------------+--------------------------------------------------+\n");

    // Summary
    unsigned long total_size = 0;
    int file_count = 0, dir_count = 0;
    for (int i = 0; i < result_count; i++) {
        total_size += results[i].size;
        if (results[i].type == 0) file_count++;
        else dir_count++;
    }
    char total_str[32];
    format_size(total_str, 32, total_size);
    neo::display::printf("\n  Files: %d  Dirs: %d  Total size: %s\n", file_count, dir_count, total_str);
    neo::display::printf("  [N]ext page  [P]rev page  [S] New search  [Q] Quit\n");
}

static void run_search() {
    result_count = 0;
    files_scanned = 0;
    dirs_scanned = 0;

    draw_header();
    neo::display::set_cursor(2, 3);
    neo::display::set_fg(14);
    neo::display::printf("Searching...");
    neo::display::set_fg(7);

    for (int p = 0; p < criteria.path_count; p++) {
        neo::display::set_cursor(2, 4 + p);
        neo::display::printf("  Scanning: %s", criteria.search_paths[p]);
        search_dir(criteria.search_paths[p], 0);
    }

    neo::display::set_cursor(2, 5 + criteria.path_count);
    neo::display::set_fg(10);
    neo::display::printf("Search complete. %d results found.", result_count);
    neo::display::set_fg(7);
    neo::timer::delay_ms(500);
}

static unsigned long parse_size(const char* str) {
    unsigned long val = 0;
    int i = 0;
    while (str[i] >= '0' && str[i] <= '9') {
        val = val * 10 + (str[i] - '0');
        i++;
    }
    // Check for suffixes
    if (str[i] == 'k' || str[i] == 'K') val *= 1024;
    else if (str[i] == 'm' || str[i] == 'M') val *= 1024 * 1024;
    return val;
}

static void interactive_setup() {
    draw_header();
    char buf[128];

    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Search Configuration\n\n");
    neo::display::set_color(7, 0);

    // Pattern
    neo::display::printf("  File pattern (* and ? wildcards, e.g. *.txt): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    if (buf[0] == 0) neo_strcpy(buf, "*");
    neo_strncpy(criteria.pattern, buf, 63);
    criteria.pattern[63] = 0;

    // Search paths
    neo::display::printf("  Search path (default /): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    criteria.path_count = 0;
    if (buf[0] == 0) {
        neo_strcpy(criteria.search_paths[0], "/");
        criteria.path_count = 1;
    } else {
        // Parse comma-separated paths
        int bi = 0;
        while (buf[bi] && criteria.path_count < MAX_SEARCH_PATHS) {
            int pi = 0;
            while (buf[bi] && buf[bi] != ',' && pi < MAX_PATH_LEN - 1) {
                criteria.search_paths[criteria.path_count][pi++] = buf[bi++];
            }
            criteria.search_paths[criteria.path_count][pi] = 0;
            if (pi > 0) criteria.path_count++;
            if (buf[bi] == ',') bi++;
        }
    }

    // Type filter
    neo::display::printf("  Type [A]ll / [F]iles / [D]irs (default: all): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    criteria.type_filter = -1;
    if (buf[0] == 'f' || buf[0] == 'F') criteria.type_filter = 0;
    if (buf[0] == 'd' || buf[0] == 'D') criteria.type_filter = 1;

    // Size filters
    neo::display::printf("  Minimum size (e.g. 1K, 10M, 0 for none): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    criteria.min_size = parse_size(buf);

    neo::display::printf("  Maximum size (e.g. 100K, 1M, 0 for none): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    criteria.max_size = parse_size(buf);

    // Case sensitivity
    neo::display::printf("  Case sensitive? [Y/N] (default: N): ");
    neo::console::getline(buf, sizeof(buf), nullptr);
    criteria.case_sensitive = (buf[0] == 'y' || buf[0] == 'Y');
}

static void quick_search(const char* pattern, const char* path) {
    neo_strncpy(criteria.pattern, pattern, 63);
    criteria.pattern[63] = 0;
    criteria.type_filter = -1;
    criteria.min_size = 0;
    criteria.max_size = 0;
    criteria.case_sensitive = false;
    criteria.path_count = 1;
    if (path && path[0]) {
        neo_strncpy(criteria.search_paths[0], path, MAX_PATH_LEN - 1);
    } else {
        neo_strcpy(criteria.search_paths[0], "/");
    }
    criteria.search_paths[0][MAX_PATH_LEN - 1] = 0;
}

}  // namespace neofind

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&neofind::criteria, 0, sizeof(neofind::criteria));

    // Command-line mode
    if (argc > 1) {
        const char* path = "/";
        if (argc > 2) path = argv[2];
        neofind::quick_search(argv[1], path);
        neofind::run_search();
        neofind::display_results(0);

        // Page through results
        int page = 0;
        while (true) {
            if (neo::keyboard::key_available()) {
                unsigned char sc = neo::keyboard::read_scancode();
                char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
                if (ch == 'q' || ch == 'Q') return;
                if (ch == 'n' || ch == 'N') { page++; neofind::display_results(page); }
                if (ch == 'p' || ch == 'P') { if (page > 0) page--; neofind::display_results(page); }
                if (ch == 's' || ch == 'S') break;  // New search
            }
            neo::proc::yield();
        }
    }

    // Interactive mode
    while (true) {
        neofind::interactive_setup();
        neofind::run_search();

        int page = 0;
        neofind::display_results(page);

        while (true) {
            if (neo::keyboard::key_available()) {
                unsigned char sc = neo::keyboard::read_scancode();
                char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
                if (ch == 'q' || ch == 'Q') return;
                if (ch == 'n' || ch == 'N') { page++; neofind::display_results(page); }
                if (ch == 'p' || ch == 'P') { if (page > 0) page--; neofind::display_results(page); }
                if (ch == 's' || ch == 'S') break;  // New search
            }
            neo::proc::yield();
        }
    }
}
