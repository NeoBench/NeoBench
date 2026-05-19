#include "../include/neobench.h"
#include "../lib/string.h"

// NeoWrite - Word Processor for NeoBench
// Features: document editing, word wrap, find/replace, load/save, formatting

namespace neowrite {

static const int MAX_LINES = 2000;
static const int MAX_LINE_LEN = INODE_SIZE;
static const int MAX_DOC_NAME = 64;
static const int TAB_SIZE = 4;

// Formatting markers embedded in text
static const char MARK_BOLD_ON = 0x01;
static const char MARK_BOLD_OFF = 0x02;
static const char MARK_UNDERLINE_ON = 0x03;
static const char MARK_UNDERLINE_OFF = 0x04;

struct Line {
    char text[MAX_LINE_LEN];
    int len;
};

struct Document {
    Line lines[MAX_LINES];
    int line_count;
    char filename[MAX_DOC_NAME];
    bool modified;
    int cursor_x;
    int cursor_y;
    int scroll_x;
    int scroll_y;
    int left_margin;
    int right_margin;
    int word_count;
};

static Document doc;
static int screen_w, screen_h;
static int edit_top, edit_bottom;
static bool running;
static bool insert_mode;

// Find/replace state
static char find_buf[80];
static char replace_buf[80];

static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static bool str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

static int visible_len(const char* text, int len) {
    int vis = 0;
    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c == MARK_BOLD_ON || c == MARK_BOLD_OFF ||
            c == MARK_UNDERLINE_ON || c == MARK_UNDERLINE_OFF)
            continue;
        vis++;
    }
    return vis;
}

static void init_document() {
    for (int i = 0; i < MAX_LINES; i++) {
        doc.lines[i].text[0] = 0;
        doc.lines[i].len = 0;
    }
    doc.line_count = 1;
    doc.filename[0] = 0;
    doc.modified = false;
    doc.cursor_x = 0;
    doc.cursor_y = 0;
    doc.scroll_x = 0;
    doc.scroll_y = 0;
    doc.left_margin = 2;
    doc.right_margin = 2;
    doc.word_count = 0;
    insert_mode = true;
    find_buf[0] = 0;
    replace_buf[0] = 0;
}

static void count_words() {
    int count = 0;
    for (int i = 0; i < doc.line_count; i++) {
        bool in_word = false;
        for (int j = 0; j < doc.lines[i].len; j++) {
            char c = doc.lines[i].text[j];
            if (c == MARK_BOLD_ON || c == MARK_BOLD_OFF ||
                c == MARK_UNDERLINE_ON || c == MARK_UNDERLINE_OFF)
                continue;
            if (c > ' ') {
                if (!in_word) { count++; in_word = true; }
            } else {
                in_word = false;
            }
        }
    }
    doc.word_count = count;
}

static int total_chars() {
    int count = 0;
    for (int i = 0; i < doc.line_count; i++) {
        count += visible_len(doc.lines[i].text, doc.lines[i].len);
    }
    return count;
}

static void draw_title_bar() {
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0x000000, 0x5599DD);
    neo::display::set_bold(true);
    neo::display::puts(" NeoWrite ");
    neo::display::set_bold(false);
    neo::display::set_color(0xFFFFFF, 0x5599DD);
    if (doc.filename[0]) {
        neo::display::puts(doc.filename);
    } else {
        neo::display::puts("[Untitled]");
    }
    if (doc.modified) neo::display::puts(" *");
    // Pad rest of line
    int cur = 12 + str_len(doc.filename[0] ? doc.filename : "[Untitled]") + (doc.modified ? 2 : 0);
    for (int i = cur; i < screen_w; i++) neo::display::putchar(' ');
}

static void draw_menu_bar() {
    neo::display::set_cursor(0, 1);
    neo::display::set_color(0xCCCCCC, 0x333355);
    char menu[] = " F1:Help  F2:Save  F3:Load  F5:Find  F6:Replace  F7:Bold  F8:Underline  ESC:Quit ";
    neo::display::puts(menu);
    int mlen = str_len(menu);
    for (int i = mlen; i < screen_w; i++) neo::display::putchar(' ');
}

static void draw_line(int doc_line, int screen_line) {
    neo::display::set_cursor(0, screen_line);
    neo::display::set_color(0x555577, 0x111122);

    // Line number gutter
    char numbuf[8];
    int ln = doc_line + 1;
    ksprintf(numbuf, 8, "%4d", ln);
    neo::display::puts(numbuf);
    neo::display::set_color(0x334455, 0x111122);
    neo::display::puts("| ");

    if (doc_line >= doc.line_count) {
        neo::display::set_color(0x333344, 0x111122);
        neo::display::putchar('~');
        neo::display::clear_eol();
        return;
    }

    neo::display::set_color(0xDDDDDD, 0x111122);
    Line* line = &doc.lines[doc_line];
    int col = 0;
    int avail = screen_w - 6; // gutter width

    bool bold = false;
    bool underline = false;

    for (int i = 0; i < line->len && col < avail + doc.scroll_x; i++) {
        char c = line->text[i];
        if (c == MARK_BOLD_ON) { bold = true; neo::display::set_bold(true); continue; }
        if (c == MARK_BOLD_OFF) { bold = false; neo::display::set_bold(false); continue; }
        if (c == MARK_UNDERLINE_ON) { underline = true; neo::display::set_color(0xFFFF88, 0x111122); continue; }
        if (c == MARK_UNDERLINE_OFF) { underline = false; neo::display::set_color(0xDDDDDD, 0x111122); continue; }

        if (col >= doc.scroll_x) {
            neo::display::putchar(c);
        }
        col++;
    }
    neo::display::set_bold(false);
    neo::display::clear_eol();
}

static void draw_status_bar() {
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0x000000, 0x55AA55);

    count_words();
    char status[128];
    ksprintf(status, 128, " Ln %d/%d  Col %d  Words: %d  Chars: %d  %s ",
             doc.cursor_y + 1, doc.line_count,
             doc.cursor_x + 1,
             doc.word_count, total_chars(),
             insert_mode ? "INS" : "OVR");
    neo::display::puts(status);
    int slen = str_len(status);
    for (int i = slen; i < screen_w; i++) neo::display::putchar(' ');
}

static void draw_screen() {
    draw_title_bar();
    draw_menu_bar();
    edit_top = 2;
    edit_bottom = screen_h - 2;

    for (int y = edit_top; y <= edit_bottom; y++) {
        int doc_line = doc.scroll_y + (y - edit_top);
        draw_line(doc_line, y);
    }
    draw_status_bar();

    // Position cursor
    int cx = doc.cursor_x - doc.scroll_x + 6; // gutter offset
    int cy = doc.cursor_y - doc.scroll_y + edit_top;
    neo::display::set_cursor(cx, cy);
    neo::display::set_color(0xDDDDDD, 0x111122);
}

static void ensure_cursor_visible() {
    int vis_lines = edit_bottom - edit_top + 1;
    if (doc.cursor_y < doc.scroll_y) doc.scroll_y = doc.cursor_y;
    if (doc.cursor_y >= doc.scroll_y + vis_lines) doc.scroll_y = doc.cursor_y - vis_lines + 1;
    if (doc.scroll_y < 0) doc.scroll_y = 0;

    int vis_cols = screen_w - 6;
    if (doc.cursor_x < doc.scroll_x) doc.scroll_x = doc.cursor_x;
    if (doc.cursor_x >= doc.scroll_x + vis_cols) doc.scroll_x = doc.cursor_x - vis_cols + 1;
    if (doc.scroll_x < 0) doc.scroll_x = 0;
}

static void clamp_cursor() {
    if (doc.cursor_y < 0) doc.cursor_y = 0;
    if (doc.cursor_y >= doc.line_count) doc.cursor_y = doc.line_count - 1;
    int len = visible_len(doc.lines[doc.cursor_y].text, doc.lines[doc.cursor_y].len);
    if (doc.cursor_x > len) doc.cursor_x = len;
    if (doc.cursor_x < 0) doc.cursor_x = 0;
}

static void insert_char_at(int line, int pos, char c) {
    Line* ln = &doc.lines[line];
    if (ln->len >= MAX_LINE_LEN - 1) return;
    for (int i = ln->len; i > pos; i--) {
        ln->text[i] = ln->text[i - 1];
    }
    ln->text[pos] = c;
    ln->len++;
    ln->text[ln->len] = 0;
}

static void delete_char_at(int line, int pos) {
    Line* ln = &doc.lines[line];
    if (pos < 0 || pos >= ln->len) return;
    for (int i = pos; i < ln->len - 1; i++) {
        ln->text[i] = ln->text[i + 1];
    }
    ln->len--;
    ln->text[ln->len] = 0;
}

static void insert_line(int at) {
    if (doc.line_count >= MAX_LINES) return;
    for (int i = doc.line_count; i > at; i--) {
        neo_memcpy(&doc.lines[i], &doc.lines[i - 1], sizeof(Line));
    }
    doc.lines[at].text[0] = 0;
    doc.lines[at].len = 0;
    doc.line_count++;
}

static void delete_line(int at) {
    if (doc.line_count <= 1) return;
    for (int i = at; i < doc.line_count - 1; i++) {
        neo_memcpy(&doc.lines[i], &doc.lines[i + 1], sizeof(Line));
    }
    doc.line_count--;
}

static void split_line() {
    if (doc.line_count >= MAX_LINES) return;
    Line* cur = &doc.lines[doc.cursor_y];
    insert_line(doc.cursor_y + 1);
    Line* next = &doc.lines[doc.cursor_y + 1];
    int pos = doc.cursor_x;
    int rem = cur->len - pos;
    if (rem > 0) {
        neo_memcpy(next->text, cur->text + pos, rem);
        next->len = rem;
        next->text[rem] = 0;
    }
    cur->len = pos;
    cur->text[pos] = 0;
    doc.cursor_y++;
    doc.cursor_x = 0;
    doc.modified = true;
}

static void join_line_up() {
    if (doc.cursor_y <= 0) return;
    Line* prev = &doc.lines[doc.cursor_y - 1];
    Line* cur = &doc.lines[doc.cursor_y];
    int new_x = prev->len;
    if (prev->len + cur->len < MAX_LINE_LEN) {
        neo_memcpy(prev->text + prev->len, cur->text, cur->len);
        prev->len += cur->len;
        prev->text[prev->len] = 0;
        delete_line(doc.cursor_y);
        doc.cursor_y--;
        doc.cursor_x = new_x;
        doc.modified = true;
    }
}

static void word_wrap_line(int line_idx) {
    int wrap_col = screen_w - 6 - doc.left_margin - doc.right_margin;
    if (wrap_col < 20) wrap_col = 60;
    Line* ln = &doc.lines[line_idx];
    if (visible_len(ln->text, ln->len) <= wrap_col) return;

    // Find last space before wrap column
    int break_pos = -1;
    int vis = 0;
    for (int i = 0; i < ln->len; i++) {
        char c = ln->text[i];
        if (c == MARK_BOLD_ON || c == MARK_BOLD_OFF ||
            c == MARK_UNDERLINE_ON || c == MARK_UNDERLINE_OFF)
            continue;
        vis++;
        if (c == ' ' && vis <= wrap_col) break_pos = i;
    }
    if (break_pos < 0) return;

    insert_line(line_idx + 1);
    Line* next = &doc.lines[line_idx + 1];
    int rem = ln->len - break_pos - 1;
    if (rem > 0) {
        neo_memcpy(next->text, ln->text + break_pos + 1, rem);
        next->len = rem;
        next->text[rem] = 0;
    }
    ln->len = break_pos;
    ln->text[break_pos] = 0;

    if (doc.cursor_y == line_idx && doc.cursor_x > break_pos) {
        doc.cursor_y++;
        doc.cursor_x -= break_pos + 1;
    }
}

static void prompt_input(const char* prompt, char* buf, int max) {
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0xFFFFFF, 0x444488);
    neo::display::puts(prompt);
    int plen = str_len(prompt);
    for (int i = plen; i < screen_w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(plen, screen_h - 1);

    int pos = 0;
    buf[0] = 0;
    while (true) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            if (ch == '\n' || ch == '\r') break;
            if (sc == 0x01) { buf[0] = 0; return; } // ESC
            if (ch == 8 || sc == 0x0E) {
                if (pos > 0) {
                    pos--;
                    buf[pos] = 0;
                    neo::display::cursor_left(1);
                    neo::display::putchar(' ');
                    neo::display::cursor_left(1);
                }
            } else if (ch >= ' ' && pos < max - 1) {
                buf[pos++] = ch;
                buf[pos] = 0;
                neo::display::putchar(ch);
            }
        }
        neo::proc::yield();
    }
}

static void do_find() {
    prompt_input("Find: ", find_buf, 80);
    if (find_buf[0] == 0) return;

    int flen = str_len(find_buf);
    for (int y = doc.cursor_y; y < doc.line_count; y++) {
        int start_x = (y == doc.cursor_y) ? doc.cursor_x + 1 : 0;
        Line* ln = &doc.lines[y];
        for (int x = start_x; x <= ln->len - flen; x++) {
            bool match = true;
            for (int k = 0; k < flen; k++) {
                if (ln->text[x + k] != find_buf[k]) { match = false; break; }
            }
            if (match) {
                doc.cursor_y = y;
                doc.cursor_x = x;
                return;
            }
        }
    }
    // Wrap around from beginning
    for (int y = 0; y <= doc.cursor_y; y++) {
        Line* ln = &doc.lines[y];
        int end_x = (y == doc.cursor_y) ? doc.cursor_x : ln->len - flen;
        for (int x = 0; x <= end_x; x++) {
            bool match = true;
            for (int k = 0; k < flen; k++) {
                if (ln->text[x + k] != find_buf[k]) { match = false; break; }
            }
            if (match) {
                doc.cursor_y = y;
                doc.cursor_x = x;
                return;
            }
        }
    }
}

static void do_replace() {
    prompt_input("Find: ", find_buf, 80);
    if (find_buf[0] == 0) return;
    prompt_input("Replace with: ", replace_buf, 80);

    int flen = str_len(find_buf);
    int rlen = str_len(replace_buf);
    int replaced = 0;

    for (int y = 0; y < doc.line_count; y++) {
        Line* ln = &doc.lines[y];
        for (int x = 0; x <= ln->len - flen; x++) {
            bool match = true;
            for (int k = 0; k < flen; k++) {
                if (ln->text[x + k] != find_buf[k]) { match = false; break; }
            }
            if (match) {
                // Remove old, insert new
                int diff = rlen - flen;
                if (ln->len + diff >= MAX_LINE_LEN) continue;

                if (diff > 0) {
                    for (int i = ln->len; i >= x + flen; i--)
                        ln->text[i + diff] = ln->text[i];
                } else if (diff < 0) {
                    for (int i = x + flen; i <= ln->len; i++)
                        ln->text[i + diff] = ln->text[i];
                }
                for (int k = 0; k < rlen; k++)
                    ln->text[x + k] = replace_buf[k];
                ln->len += diff;
                ln->text[ln->len] = 0;
                x += rlen - 1;
                replaced++;
                doc.modified = true;
            }
        }
    }
}

static void do_save() {
    if (doc.filename[0] == 0) {
        prompt_input("Save as: ", doc.filename, MAX_DOC_NAME);
        if (doc.filename[0] == 0) return;
    }

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, doc.filename, 1) != 0) return;

    for (int i = 0; i < doc.line_count; i++) {
        neo::filesystem::write(fh, doc.lines[i].text, doc.lines[i].len);
        char nl = '\n';
        neo::filesystem::write(fh, &nl, 1);
    }
    neo::filesystem::close(fh);
    doc.modified = false;
}

static void do_load() {
    char fname[MAX_DOC_NAME];
    prompt_input("Load file: ", fname, MAX_DOC_NAME);
    if (fname[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, fname, 0) != 0) return;

    init_document();
    str_copy(doc.filename, fname, MAX_DOC_NAME);

    char buf[512];
    int line_idx = 0;
    int col = 0;
    int n;
    while ((n = neo::filesystem::read(fh, buf, 512)) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                doc.lines[line_idx].len = col;
                doc.lines[line_idx].text[col] = 0;
                line_idx++;
                if (line_idx >= MAX_LINES) break;
                col = 0;
            } else if (col < MAX_LINE_LEN - 1) {
                doc.lines[line_idx].text[col++] = buf[i];
            }
        }
    }
    if (col > 0) {
        doc.lines[line_idx].len = col;
        doc.lines[line_idx].text[col] = 0;
        line_idx++;
    }
    doc.line_count = line_idx > 0 ? line_idx : 1;
    neo::filesystem::close(fh);
}

static void toggle_bold() {
    Line* ln = &doc.lines[doc.cursor_y];
    if (ln->len + 2 >= MAX_LINE_LEN) return;
    insert_char_at(doc.cursor_y, doc.cursor_x, MARK_BOLD_ON);
    insert_char_at(doc.cursor_y, doc.cursor_x + 1, MARK_BOLD_OFF);
    doc.cursor_x++;
    doc.modified = true;
}

static void toggle_underline() {
    Line* ln = &doc.lines[doc.cursor_y];
    if (ln->len + 2 >= MAX_LINE_LEN) return;
    insert_char_at(doc.cursor_y, doc.cursor_x, MARK_UNDERLINE_ON);
    insert_char_at(doc.cursor_y, doc.cursor_x + 1, MARK_UNDERLINE_OFF);
    doc.cursor_x++;
    doc.modified = true;
}

static void show_help() {
    neo::display::clear();
    neo::display::set_color(0x55DDFF, 0x111122);
    neo::display::set_cursor(0, 0);
    neo::display::set_bold(true);
    neo::display::puts("=== NeoWrite Help ===\n\n");
    neo::display::set_bold(false);
    neo::display::set_color(0xDDDDDD, 0x111122);
    neo::display::puts("  Arrow Keys   - Move cursor\n");
    neo::display::puts("  Home/End     - Start/end of line\n");
    neo::display::puts("  PgUp/PgDn    - Page up/down\n");
    neo::display::puts("  Enter        - New line\n");
    neo::display::puts("  Backspace    - Delete left\n");
    neo::display::puts("  Delete       - Delete right\n");
    neo::display::puts("  Insert       - Toggle insert/overwrite\n\n");
    neo::display::puts("  F1           - This help screen\n");
    neo::display::puts("  F2           - Save document\n");
    neo::display::puts("  F3           - Load document\n");
    neo::display::puts("  F5           - Find text\n");
    neo::display::puts("  F6           - Find and replace\n");
    neo::display::puts("  F7           - Insert bold markers\n");
    neo::display::puts("  F8           - Insert underline markers\n");
    neo::display::puts("  ESC          - Quit\n\n");
    neo::display::puts("  Press any key to return...\n");

    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

static void handle_key(int scancode) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(scancode, shift);

    switch (scancode) {
        case 0x01: // ESC
            running = false;
            return;
        case 0x3B: show_help(); return;          // F1
        case 0x3C: do_save(); return;             // F2
        case 0x3D: do_load(); return;             // F3
        case 0x3F: do_find(); return;             // F5
        case 0x40: do_replace(); return;          // F6
        case 0x41: toggle_bold(); return;         // F7
        case 0x42: toggle_underline(); return;    // F8

        case 0x48: // Up
            doc.cursor_y--;
            clamp_cursor();
            return;
        case 0x50: // Down
            doc.cursor_y++;
            clamp_cursor();
            return;
        case 0x4B: // Left
            if (doc.cursor_x > 0) doc.cursor_x--;
            else if (doc.cursor_y > 0) {
                doc.cursor_y--;
                doc.cursor_x = doc.lines[doc.cursor_y].len;
            }
            return;
        case 0x4D: // Right
            if (doc.cursor_x < doc.lines[doc.cursor_y].len) doc.cursor_x++;
            else if (doc.cursor_y < doc.line_count - 1) {
                doc.cursor_y++;
                doc.cursor_x = 0;
            }
            return;
        case 0x47: // Home
            doc.cursor_x = 0;
            return;
        case 0x4F: // End
            doc.cursor_x = doc.lines[doc.cursor_y].len;
            return;
        case 0x49: { // PgUp
            int page = edit_bottom - edit_top;
            doc.cursor_y -= page;
            doc.scroll_y -= page;
            if (doc.scroll_y < 0) doc.scroll_y = 0;
            clamp_cursor();
            return;
        }
        case 0x51: { // PgDn
            int page = edit_bottom - edit_top;
            doc.cursor_y += page;
            doc.scroll_y += page;
            clamp_cursor();
            return;
        }
        case 0x52: // Insert
            insert_mode = !insert_mode;
            return;
        case 0x53: // Delete
            if (doc.cursor_x < doc.lines[doc.cursor_y].len) {
                delete_char_at(doc.cursor_y, doc.cursor_x);
                doc.modified = true;
            } else if (doc.cursor_y < doc.line_count - 1) {
                // Join next line
                Line* cur = &doc.lines[doc.cursor_y];
                Line* next = &doc.lines[doc.cursor_y + 1];
                if (cur->len + next->len < MAX_LINE_LEN) {
                    neo_memcpy(cur->text + cur->len, next->text, next->len);
                    cur->len += next->len;
                    cur->text[cur->len] = 0;
                    delete_line(doc.cursor_y + 1);
                    doc.modified = true;
                }
            }
            return;
        case 0x0E: // Backspace
            if (doc.cursor_x > 0) {
                doc.cursor_x--;
                delete_char_at(doc.cursor_y, doc.cursor_x);
                doc.modified = true;
            } else {
                join_line_up();
            }
            return;
    }

    if (ch == '\n' || ch == '\r') {
        split_line();
        return;
    }

    if (ch >= ' ' && ch < 127) {
        if (insert_mode) {
            insert_char_at(doc.cursor_y, doc.cursor_x, ch);
        } else {
            if (doc.cursor_x < doc.lines[doc.cursor_y].len)
                doc.lines[doc.cursor_y].text[doc.cursor_x] = ch;
            else
                insert_char_at(doc.cursor_y, doc.cursor_x, ch);
        }
        doc.cursor_x++;
        doc.modified = true;

        // Auto word-wrap
        word_wrap_line(doc.cursor_y);
    }
}

} // namespace neowrite

extern "C" void app_main(int argc, char** argv) {
    using namespace neowrite;

    screen_w = neo::display::get_width();
    screen_h = neo::display::get_height();
    edit_top = 2;
    edit_bottom = screen_h - 2;
    running = true;

    init_document();

    // Load file from command line if provided
    if (argc > 1) {
        str_copy(doc.filename, argv[1], MAX_DOC_NAME);
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, doc.filename, 0) == 0) {
            char buf[512];
            int line_idx = 0, col = 0, n;
            while ((n = neo::filesystem::read(fh, buf, 512)) > 0) {
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '\n') {
                        doc.lines[line_idx].len = col;
                        doc.lines[line_idx].text[col] = 0;
                        line_idx++;
                        col = 0;
                        if (line_idx >= MAX_LINES) break;
                    } else if (col < MAX_LINE_LEN - 1) {
                        doc.lines[line_idx].text[col++] = buf[i];
                    }
                }
            }
            if (col > 0) {
                doc.lines[line_idx].len = col;
                doc.lines[line_idx].text[col] = 0;
                line_idx++;
            }
            doc.line_count = line_idx > 0 ? line_idx : 1;
            neo::filesystem::close(fh);
        }
    }

    neo::display::clear();
    draw_screen();

    while (running) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            handle_key(sc);
            ensure_cursor_visible();
            draw_screen();
        }
        neo::proc::yield();
    }

    neo::display::clear();
    neo::display::set_color(0xDDDDDD, 0x000000);
    neo::display::set_cursor(0, 0);
}
