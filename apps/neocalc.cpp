#include "../include/neobench.h"
#include "../lib/string.h"

// NeoCalc - Spreadsheet for NeoBench
// Features: 26x100 grid, formulas, cell references, SUM/AVG/MIN/MAX/COUNT, navigation

namespace neocalc {

static const int MAX_COLS = 26;
static const int MAX_ROWS = 100;
static const int MAX_CELL_TEXT = 64;
static const int DEFAULT_COL_WIDTH = 10;

enum CellType { CELL_EMPTY, CELL_NUMBER, CELL_TEXT, CELL_FORMULA };

struct Cell {
    char raw[MAX_CELL_TEXT];      // raw input
    char display[MAX_CELL_TEXT];  // display text
    double value;
    CellType type;
    bool error;
};

static Cell grid[MAX_ROWS][MAX_COLS];
static int col_widths[MAX_COLS];
static int cursor_col, cursor_row;
static int scroll_col, scroll_row;
static int screen_w, screen_h;
static bool running;
static bool editing;
static char edit_buf[MAX_CELL_TEXT];
static int edit_pos;
static char filename[64];
static bool modified;

static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void str_copy(char* d, const char* s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void init_grid() {
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) {
            grid[r][c].raw[0] = 0;
            grid[r][c].display[0] = 0;
            grid[r][c].value = 0.0;
            grid[r][c].type = CELL_EMPTY;
            grid[r][c].error = false;
        }
    for (int c = 0; c < MAX_COLS; c++)
        col_widths[c] = DEFAULT_COL_WIDTH;
    cursor_col = 0;
    cursor_row = 0;
    scroll_col = 0;
    scroll_row = 0;
    editing = false;
    filename[0] = 0;
    modified = false;
}

// Parse cell reference like "A1" -> col=0, row=0
static bool parse_ref(const char* s, int& col, int& row, int& consumed) {
    if (s[0] < 'A' || s[0] > 'Z') return false;
    col = s[0] - 'A';
    int i = 1;
    row = 0;
    if (s[i] < '0' || s[i] > '9') return false;
    while (s[i] >= '0' && s[i] <= '9') {
        row = row * 10 + (s[i] - '0');
        i++;
    }
    row--; // 1-based to 0-based
    consumed = i;
    return row >= 0 && row < MAX_ROWS && col >= 0 && col < MAX_COLS;
}

// Parse range like "A1:B5"
static bool parse_range(const char* s, int& c1, int& r1, int& c2, int& r2, int& consumed) {
    int n1, n2;
    if (!parse_ref(s, c1, r1, n1)) return false;
    if (s[n1] != ':') return false;
    if (!parse_ref(s + n1 + 1, c2, r2, n2)) return false;
    consumed = n1 + 1 + n2;
    return true;
}

// Simple number parser
static double parse_number(const char* s, int& consumed) {
    double result = 0;
    double frac = 0;
    double div = 1;
    bool neg = false;
    int i = 0;
    if (s[i] == '-') { neg = true; i++; }
    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10 + (s[i] - '0');
        i++;
    }
    if (s[i] == '.') {
        i++;
        while (s[i] >= '0' && s[i] <= '9') {
            frac = frac * 10 + (s[i] - '0');
            div *= 10;
            i++;
        }
        result += frac / div;
    }
    consumed = i;
    return neg ? -result : result;
}

static void recalc_cell(int row, int col);

// Evaluate expression (recursive descent)
static double eval_expr(const char* s, int& pos, bool& error);

static void skip_spaces(const char* s, int& pos) {
    while (s[pos] == ' ') pos++;
}

// Check for function: SUM, AVG, MIN, MAX, COUNT
static bool try_function(const char* s, int& pos, double& result, bool& error) {
    const char* funcs[] = { "SUM", "AVG", "MIN", "MAX", "COUNT" };
    int fid = -1;
    for (int f = 0; f < 5; f++) {
        int fl = str_len(funcs[f]);
        bool match = true;
        for (int i = 0; i < fl; i++) {
            if (s[pos + i] != funcs[f][i]) { match = false; break; }
        }
        if (match && s[pos + fl] == '(') {
            fid = f;
            pos += fl + 1; // skip "FUNC("
            break;
        }
    }
    if (fid < 0) return false;

    int c1, r1, c2, r2, consumed;
    if (!parse_range(s + pos, c1, r1, c2, r2, consumed)) { error = true; return true; }
    pos += consumed;
    if (s[pos] == ')') pos++;

    double sum = 0, mn = 1e30, mx = -1e30;
    int cnt = 0;
    for (int r = r1; r <= r2; r++) {
        for (int c = c1; c <= c2; c++) {
            if (grid[r][c].type == CELL_NUMBER || grid[r][c].type == CELL_FORMULA) {
                double v = grid[r][c].value;
                sum += v;
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                cnt++;
            }
        }
    }

    switch (fid) {
        case 0: result = sum; break;
        case 1: result = cnt > 0 ? sum / cnt : 0; break;
        case 2: result = mn; break;
        case 3: result = mx; break;
        case 4: result = (double)cnt; break;
    }
    return true;
}

static double eval_primary(const char* s, int& pos, bool& error) {
    skip_spaces(s, pos);

    // Try function
    double fres;
    if (try_function(s, pos, fres, error)) return fres;

    // Try cell reference
    int col, row, consumed;
    if (s[pos] >= 'A' && s[pos] <= 'Z' && parse_ref(s + pos, col, row, consumed)) {
        pos += consumed;
        if (grid[row][col].type == CELL_EMPTY) return 0;
        return grid[row][col].value;
    }

    // Parenthesized expression
    if (s[pos] == '(') {
        pos++;
        double v = eval_expr(s, pos, error);
        skip_spaces(s, pos);
        if (s[pos] == ')') pos++;
        return v;
    }

    // Number
    int nc;
    double num = parse_number(s + pos, nc);
    if (nc == 0) { error = true; return 0; }
    pos += nc;
    return num;
}

static double eval_term(const char* s, int& pos, bool& error) {
    double left = eval_primary(s, pos, error);
    while (true) {
        skip_spaces(s, pos);
        if (s[pos] == '*') { pos++; left *= eval_primary(s, pos, error); }
        else if (s[pos] == '/') {
            pos++;
            double r = eval_primary(s, pos, error);
            if (r == 0) { error = true; return 0; }
            left /= r;
        } else break;
    }
    return left;
}

static double eval_expr(const char* s, int& pos, bool& error) {
    double left = eval_term(s, pos, error);
    while (true) {
        skip_spaces(s, pos);
        if (s[pos] == '+') { pos++; left += eval_term(s, pos, error); }
        else if (s[pos] == '-') { pos++; left -= eval_term(s, pos, error); }
        else break;
    }
    return left;
}

static void format_number(double val, char* buf, int max) {
    int ival = (int)val;
    int frac = (int)((val - ival) * 100);
    if (frac < 0) frac = -frac;
    if (frac == 0) {
        ksprintf(buf, max, "%d", ival);
    } else {
        ksprintf(buf, max, "%d.%02d", ival, frac);
    }
}

static void recalc_cell(int row, int col) {
    Cell* c = &grid[row][col];
    if (c->raw[0] == 0) {
        c->type = CELL_EMPTY;
        c->display[0] = 0;
        c->value = 0;
        return;
    }

    if (c->raw[0] == '=') {
        c->type = CELL_FORMULA;
        c->error = false;
        int pos = 1;
        c->value = eval_expr(c->raw, pos, c->error);
        if (c->error) {
            str_copy(c->display, "#ERR", MAX_CELL_TEXT);
        } else {
            format_number(c->value, c->display, MAX_CELL_TEXT);
        }
        return;
    }

    // Try number
    int consumed;
    double num = parse_number(c->raw, consumed);
    if (consumed > 0 && c->raw[consumed] == 0) {
        c->type = CELL_NUMBER;
        c->value = num;
        format_number(num, c->display, MAX_CELL_TEXT);
        return;
    }

    c->type = CELL_TEXT;
    c->value = 0;
    str_copy(c->display, c->raw, MAX_CELL_TEXT);
}

static void recalc_all() {
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            recalc_cell(r, c);
    // Second pass for dependencies
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            if (grid[r][c].type == CELL_FORMULA)
                recalc_cell(r, c);
}

static void draw_title_bar() {
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0x000000, 0x55AA55);
    neo::display::set_bold(true);
    neo::display::puts(" NeoCalc ");
    neo::display::set_bold(false);
    if (filename[0]) neo::display::puts(filename);
    else neo::display::puts("[New Sheet]");
    if (modified) neo::display::puts(" *");
    int pad = screen_w - 30;
    for (int i = 0; i < pad; i++) neo::display::putchar(' ');
}

static void draw_cell_info() {
    neo::display::set_cursor(0, 1);
    neo::display::set_color(0xFFFFFF, 0x333355);
    char ref[8];
    ref[0] = 'A' + cursor_col;
    ksprintf(ref + 1, 6, "%d", cursor_row + 1);
    neo::display::puts(" [");
    neo::display::set_bold(true);
    neo::display::puts(ref);
    neo::display::set_bold(false);
    neo::display::puts("] ");

    if (editing) {
        neo::display::set_color(0xFFFF88, 0x333355);
        neo::display::puts(edit_buf);
    } else {
        Cell* c = &grid[cursor_row][cursor_col];
        if (c->type == CELL_FORMULA) {
            neo::display::set_color(0x88FF88, 0x333355);
            neo::display::puts(c->raw);
        } else {
            neo::display::puts(c->raw);
        }
    }
    neo::display::clear_eol();
}

static void draw_menu() {
    neo::display::set_cursor(0, 2);
    neo::display::set_color(0xAAAAAA, 0x222244);
    neo::display::puts(" F2:Save F3:Load F5:Widen F6:Narrow F9:Recalc Enter:Edit ESC:Quit ");
    neo::display::clear_eol();
}

static void draw_grid() {
    int row_header_w = 5;
    int start_y = 3;

    // Column headers
    neo::display::set_cursor(0, start_y);
    neo::display::set_color(0x000000, 0x8888AA);
    for (int i = 0; i < row_header_w; i++) neo::display::putchar(' ');

    int x = row_header_w;
    for (int c = scroll_col; c < MAX_COLS && x < screen_w; c++) {
        int w = col_widths[c];
        char hdr[12];
        hdr[0] = 'A' + c;
        hdr[1] = 0;
        // Center header
        int pad_l = (w - 1) / 2;
        for (int i = 0; i < pad_l; i++) neo::display::putchar(' ');
        neo::display::putchar(hdr[0]);
        int pad_r = w - 1 - pad_l;
        for (int i = 0; i < pad_r; i++) neo::display::putchar(' ');
        x += w;
    }
    neo::display::clear_eol();

    // Grid rows
    int max_vis_rows = screen_h - start_y - 2;
    for (int vr = 0; vr < max_vis_rows; vr++) {
        int r = scroll_row + vr;
        int sy = start_y + 1 + vr;
        neo::display::set_cursor(0, sy);

        if (r >= MAX_ROWS) {
            neo::display::set_color(0x333344, 0x111122);
            neo::display::clear_eol();
            continue;
        }

        // Row header
        neo::display::set_color(0x000000, 0x8888AA);
        char rh[6];
        ksprintf(rh, 6, "%4d ", r + 1);
        neo::display::puts(rh);

        x = row_header_w;
        for (int c = scroll_col; c < MAX_COLS && x < screen_w; c++) {
            int w = col_widths[c];
            bool is_cursor = (r == cursor_row && c == cursor_col);
            Cell* cell = &grid[r][c];

            if (is_cursor) {
                neo::display::set_color(0x000000, 0xFFFF88);
            } else if (cell->error) {
                neo::display::set_color(0xFF4444, 0x111122);
            } else if (cell->type == CELL_FORMULA) {
                neo::display::set_color(0x88FF88, 0x111122);
            } else if (cell->type == CELL_NUMBER) {
                neo::display::set_color(0x88BBFF, 0x111122);
            } else if (cell->type == CELL_TEXT) {
                neo::display::set_color(0xDDDDDD, 0x111122);
            } else {
                neo::display::set_color(0x333344, 0x111122);
            }

            const char* disp = cell->display;
            int dlen = str_len(disp);
            // Right-align numbers, left-align text
            if (cell->type == CELL_NUMBER || cell->type == CELL_FORMULA) {
                int pad = w - dlen - 1;
                if (pad < 0) pad = 0;
                for (int i = 0; i < pad; i++) neo::display::putchar(' ');
                for (int i = 0; i < dlen && i < w - 1; i++) neo::display::putchar(disp[i]);
                neo::display::putchar(' ');
            } else {
                neo::display::putchar(' ');
                for (int i = 0; i < dlen && i < w - 2; i++) neo::display::putchar(disp[i]);
                int pad = w - dlen - 1;
                for (int i = 0; i < pad; i++) neo::display::putchar(' ');
            }

            if (is_cursor) neo::display::set_color(0xDDDDDD, 0x111122);
            x += w;
        }
        neo::display::clear_eol();
    }
}

static void draw_status() {
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0x000000, 0x55AA55);
    char status[128];
    Cell* c = &grid[cursor_row][cursor_col];
    const char* types[] = { "Empty", "Number", "Text", "Formula" };
    ksprintf(status, 128, " %c%d | Type: %s | Value: ",
             'A' + cursor_col, cursor_row + 1, types[c->type]);
    neo::display::puts(status);
    if (c->type == CELL_NUMBER || c->type == CELL_FORMULA) {
        char vbuf[20];
        format_number(c->value, vbuf, 20);
        neo::display::puts(vbuf);
    }
    neo::display::clear_eol();
}

static void draw_screen() {
    draw_title_bar();
    draw_cell_info();
    draw_menu();
    draw_grid();
    draw_status();
}

static void start_edit() {
    editing = true;
    Cell* c = &grid[cursor_row][cursor_col];
    str_copy(edit_buf, c->raw, MAX_CELL_TEXT);
    edit_pos = str_len(edit_buf);
}

static void finish_edit() {
    editing = false;
    str_copy(grid[cursor_row][cursor_col].raw, edit_buf, MAX_CELL_TEXT);
    recalc_all();
    modified = true;
}

static void cancel_edit() {
    editing = false;
    edit_buf[0] = 0;
    edit_pos = 0;
}

static void ensure_visible() {
    int row_header_w = 5;
    int start_y = 4;
    int max_vis_rows = screen_h - start_y - 2;

    if (cursor_row < scroll_row) scroll_row = cursor_row;
    if (cursor_row >= scroll_row + max_vis_rows) scroll_row = cursor_row - max_vis_rows + 1;

    // Horizontal scroll
    int x = row_header_w;
    for (int c = scroll_col; c < cursor_col; c++) x += col_widths[c];
    if (x + col_widths[cursor_col] > screen_w) {
        while (x + col_widths[cursor_col] > screen_w && scroll_col < cursor_col) {
            x -= col_widths[scroll_col];
            scroll_col++;
        }
    }
    if (cursor_col < scroll_col) scroll_col = cursor_col;
}

static void do_save() {
    if (filename[0] == 0) {
        // Prompt for filename
        neo::display::set_cursor(0, screen_h - 1);
        neo::display::set_color(0xFFFFFF, 0x444488);
        neo::display::puts("Save as: ");
        neo::display::clear_eol();
        int pos = 0;
        filename[0] = 0;
        while (true) {
            if (neo::keyboard::key_available()) {
                int sc = neo::keyboard::read_scancode();
                bool sh = neo::keyboard::is_shift_down();
                char ch = neo::keyboard::translate(sc, sh);
                if (ch == '\n' || ch == '\r') break;
                if (sc == 0x01) { filename[0] = 0; return; }
                if ((ch == 8 || sc == 0x0E) && pos > 0) {
                    pos--; filename[pos] = 0;
                    neo::display::cursor_left(1);
                    neo::display::putchar(' ');
                    neo::display::cursor_left(1);
                } else if (ch >= ' ' && pos < 60) {
                    filename[pos++] = ch;
                    filename[pos] = 0;
                    neo::display::putchar(ch);
                }
            }
            neo::proc::yield();
        }
    }
    if (filename[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, filename, 1) != 0) return;

    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (grid[r][c].raw[0]) {
                char line[128];
                ksprintf(line, 128, "%c%d\t%s\n", 'A' + c, r + 1, grid[r][c].raw);
                neo::filesystem::write(fh, line, str_len(line));
            }
        }
    }
    neo::filesystem::close(fh);
    modified = false;
}

static void do_load() {
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0xFFFFFF, 0x444488);
    neo::display::puts("Load file: ");
    neo::display::clear_eol();

    char fname[64];
    int pos = 0;
    fname[0] = 0;
    while (true) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            bool sh = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, sh);
            if (ch == '\n' || ch == '\r') break;
            if (sc == 0x01) return;
            if ((ch == 8 || sc == 0x0E) && pos > 0) {
                pos--; fname[pos] = 0;
                neo::display::cursor_left(1);
                neo::display::putchar(' ');
                neo::display::cursor_left(1);
            } else if (ch >= ' ' && pos < 60) {
                fname[pos++] = ch;
                fname[pos] = 0;
                neo::display::putchar(ch);
            }
        }
        neo::proc::yield();
    }
    if (fname[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, fname, 0) != 0) return;

    init_grid();
    str_copy(filename, fname, 64);

    char buf[1024];
    char linebuf[INODE_SIZE];
    int lpos = 0;
    int n;
    while ((n = neo::filesystem::read(fh, buf, 1024)) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                linebuf[lpos] = 0;
                // Parse: cell_ref TAB content
                if (lpos > 2) {
                    int col_v, row_v, consumed;
                    if (parse_ref(linebuf, col_v, row_v, consumed)) {
                        if (linebuf[consumed] == '\t') {
                            str_copy(grid[row_v][col_v].raw, linebuf + consumed + 1, MAX_CELL_TEXT);
                        }
                    }
                }
                lpos = 0;
            } else if (lpos < 255) {
                linebuf[lpos++] = buf[i];
            }
        }
    }
    neo::filesystem::close(fh);
    recalc_all();
}

static void handle_key(int scancode) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(scancode, shift);

    if (editing) {
        if (ch == '\n' || ch == '\r') { finish_edit(); return; }
        if (scancode == 0x01) { cancel_edit(); return; }
        if ((ch == 8 || scancode == 0x0E) && edit_pos > 0) {
            edit_pos--;
            edit_buf[edit_pos] = 0;
            return;
        }
        if (ch >= ' ' && ch < 127 && edit_pos < MAX_CELL_TEXT - 1) {
            edit_buf[edit_pos++] = ch;
            edit_buf[edit_pos] = 0;
            return;
        }
        return;
    }

    switch (scancode) {
        case 0x01: running = false; return;
        case 0x3C: do_save(); return;
        case 0x3D: do_load(); return;
        case 0x3F: // F5 widen
            if (col_widths[cursor_col] < 30) col_widths[cursor_col]++;
            return;
        case 0x40: // F6 narrow
            if (col_widths[cursor_col] > 4) col_widths[cursor_col]--;
            return;
        case 0x43: recalc_all(); return; // F9

        case 0x48: if (cursor_row > 0) cursor_row--; break;
        case 0x50: if (cursor_row < MAX_ROWS - 1) cursor_row++; break;
        case 0x4B: if (cursor_col > 0) cursor_col--; break;
        case 0x4D: if (cursor_col < MAX_COLS - 1) cursor_col++; break;
        case 0x47: cursor_col = 0; break; // Home
        case 0x4F: cursor_col = MAX_COLS - 1; break; // End
        case 0x49: cursor_row = (cursor_row > 20) ? cursor_row - 20 : 0; break;
        case 0x51: cursor_row = (cursor_row + 20 < MAX_ROWS) ? cursor_row + 20 : MAX_ROWS - 1; break;

        case 0x53: // Delete - clear cell
            grid[cursor_row][cursor_col].raw[0] = 0;
            recalc_all();
            modified = true;
            return;

        default:
            if (ch == '\n' || ch == '\r') { start_edit(); return; }
            if (ch >= ' ' && ch < 127) {
                // Start editing with typed char
                editing = true;
                edit_buf[0] = ch;
                edit_buf[1] = 0;
                edit_pos = 1;
                return;
            }
    }
    ensure_visible();
}

} // namespace neocalc

extern "C" void app_main(int argc, char** argv) {
    using namespace neocalc;

    screen_w = neo::display::get_width();
    screen_h = neo::display::get_height();
    running = true;

    init_grid();

    if (argc > 1) {
        str_copy(filename, argv[1], 64);
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, 0) == 0) {
            char buf[1024];
            char linebuf[INODE_SIZE];
            int lpos = 0, n;
            while ((n = neo::filesystem::read(fh, buf, 1024)) > 0) {
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '\n') {
                        linebuf[lpos] = 0;
                        if (lpos > 2) {
                            int cv, rv, consumed;
                            if (parse_ref(linebuf, cv, rv, consumed) && linebuf[consumed] == '\t') {
                                str_copy(grid[rv][cv].raw, linebuf + consumed + 1, MAX_CELL_TEXT);
                            }
                        }
                        lpos = 0;
                    } else if (lpos < 255) linebuf[lpos++] = buf[i];
                }
            }
            neo::filesystem::close(fh);
            recalc_all();
        }
    }

    neo::display::clear();
    draw_screen();

    while (running) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_screen();
        }
        neo::proc::yield();
    }

    neo::display::clear();
    neo::display::set_color(0xDDDDDD, 0x000000);
    neo::display::set_cursor(0, 0);
}
