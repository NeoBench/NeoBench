#include "../include/neobench.h"
#include "../lib/string.h"

// NeoBase - Database Manager for NeoBench
// Features: tables with typed columns, CRUD, simple SQL queries, form view, save/load

namespace neobase {

static const int MAX_TABLES = 8;
static const int MAX_COLUMNS = 16;
static const int MAX_ROWS = 200;
static const int MAX_NAME = 32;
static const int MAX_CELL = 64;

enum ColType { COL_TEXT, COL_NUMBER, COL_DATE };

struct Column {
    char name[MAX_NAME];
    ColType type;
    int width;
};

struct Row {
    char cells[MAX_COLUMNS][MAX_CELL];
    bool deleted;
};

struct Table {
    char name[MAX_NAME];
    Column columns[MAX_COLUMNS];
    int col_count;
    Row rows[MAX_ROWS];
    int row_count;
    bool in_use;
};

enum ViewMode { VIEW_BROWSER, VIEW_FORM, VIEW_QUERY };

static Table tables[MAX_TABLES];
static int current_table;
static int cursor_col, cursor_row;
static int scroll_col, scroll_row;
static int screen_w, screen_h;
static bool running;
static ViewMode view_mode;
static char filename[64];
static bool modified;

// Query result
static char query_buf[INODE_SIZE];
static char query_result[2048];
static int query_result_len;

// Edit state
static bool editing;
static char edit_buf[MAX_CELL];
static int edit_pos;

static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char* d, const char* s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static bool str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}
static bool str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

static int parse_int(const char* s) {
    int v = 0, neg = 0, i = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
    return neg ? -v : v;
}

static void init_tables() {
    for (int t = 0; t < MAX_TABLES; t++) {
        tables[t].in_use = false;
        tables[t].name[0] = 0;
        tables[t].col_count = 0;
        tables[t].row_count = 0;
    }
    current_table = -1;
    cursor_col = 0;
    cursor_row = 0;
    scroll_col = 0;
    scroll_row = 0;
    view_mode = VIEW_BROWSER;
    filename[0] = 0;
    modified = false;
    editing = false;
    query_buf[0] = 0;
    query_result[0] = 0;
    query_result_len = 0;
}

static Table* cur_table() {
    if (current_table < 0 || current_table >= MAX_TABLES) return 0;
    if (!tables[current_table].in_use) return 0;
    return &tables[current_table];
}

static void prompt_input(const char* prompt, char* buf, int max) {
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(15, 4);
    neo::display::puts(prompt);
    neo::display::clear_eol();
    int plen = str_len(prompt);
    neo::display::set_cursor(plen, screen_h - 1);

    int pos = 0;
    buf[0] = 0;
    while (true) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);
            if (ch == '\n' || ch == '\r') break;
            if (sc == 0x01) { buf[0] = 0; return; }
            if ((ch == 8 || sc == 0x0E) && pos > 0) {
                pos--; buf[pos] = 0;
                neo::display::cursor_left(1);
                neo::display::putchar(' ');
                neo::display::cursor_left(1);
            } else if (ch >= ' ' && pos < max - 1) {
                buf[pos++] = ch; buf[pos] = 0;
                neo::display::putchar(ch);
            }
        }
        neo::proc::yield();
    }
}

static void create_table() {
    int slot = -1;
    for (int i = 0; i < MAX_TABLES; i++) {
        if (!tables[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return;

    char name[MAX_NAME];
    prompt_input("Table name: ", name, MAX_NAME);
    if (name[0] == 0) return;

    char ncols_str[8];
    prompt_input("Number of columns (1-16): ", ncols_str, 8);
    int ncols = parse_int(ncols_str);
    if (ncols < 1 || ncols > MAX_COLUMNS) return;

    Table* t = &tables[slot];
    str_copy(t->name, name, MAX_NAME);
    t->col_count = ncols;
    t->row_count = 0;
    t->in_use = true;

    for (int c = 0; c < ncols; c++) {
        char prompt[64];
        ksprintf(prompt, 64, "Column %d name: ", c + 1);
        prompt_input(prompt, t->columns[c].name, MAX_NAME);
        if (t->columns[c].name[0] == 0) {
            ksprintf(t->columns[c].name, MAX_NAME, "Col%d", c + 1);
        }

        char type_str[8];
        prompt_input("Type (t=text, n=number, d=date): ", type_str, 8);
        if (type_str[0] == 'n' || type_str[0] == 'N')
            t->columns[c].type = COL_NUMBER;
        else if (type_str[0] == 'd' || type_str[0] == 'D')
            t->columns[c].type = COL_DATE;
        else
            t->columns[c].type = COL_TEXT;

        t->columns[c].width = 12;
    }

    current_table = slot;
    cursor_row = 0;
    cursor_col = 0;
    modified = true;
}

static void add_row() {
    Table* t = cur_table();
    if (!t || t->row_count >= MAX_ROWS) return;

    int r = t->row_count;
    t->rows[r].deleted = false;
    for (int c = 0; c < t->col_count; c++) {
        t->rows[r].cells[c][0] = 0;
    }
    t->row_count++;
    cursor_row = r;
    modified = true;
}

static void delete_row() {
    Table* t = cur_table();
    if (!t || cursor_row >= t->row_count) return;
    t->rows[cursor_row].deleted = true;
    modified = true;
}

// Sort by current column
static void sort_table() {
    Table* t = cur_table();
    if (!t || t->row_count < 2) return;

    // Simple bubble sort
    for (int i = 0; i < t->row_count - 1; i++) {
        for (int j = 0; j < t->row_count - 1 - i; j++) {
            if (t->rows[j].deleted && !t->rows[j + 1].deleted) {
                Row tmp;
                neo_memcpy(&tmp, &t->rows[j], sizeof(Row));
                neo_memcpy(&t->rows[j], &t->rows[j + 1], sizeof(Row));
                neo_memcpy(&t->rows[j + 1], &tmp, sizeof(Row));
                continue;
            }
            if (t->rows[j].deleted) continue;

            bool swap = false;
            if (t->columns[cursor_col].type == COL_NUMBER) {
                int a = parse_int(t->rows[j].cells[cursor_col]);
                int b = parse_int(t->rows[j + 1].cells[cursor_col]);
                swap = (a > b);
            } else {
                swap = (neo_strcmp(t->rows[j].cells[cursor_col],
                                   t->rows[j + 1].cells[cursor_col]) > 0);
            }
            if (swap) {
                Row tmp;
                neo_memcpy(&tmp, &t->rows[j], sizeof(Row));
                neo_memcpy(&t->rows[j], &t->rows[j + 1], sizeof(Row));
                neo_memcpy(&t->rows[j + 1], &tmp, sizeof(Row));
            }
        }
    }
}

// Simple query parser: SELECT cols FROM table WHERE col op value
static void execute_query() {
    query_result[0] = 0;
    query_result_len = 0;

    // Tokenize by spaces
    char tokens[16][MAX_CELL];
    int tc = 0;
    int qi = 0;
    while (query_buf[qi] && tc < 16) {
        while (query_buf[qi] == ' ') qi++;
        if (!query_buf[qi]) break;
        int ti = 0;
        while (query_buf[qi] && query_buf[qi] != ' ' && ti < MAX_CELL - 1) {
            tokens[tc][ti++] = query_buf[qi++];
        }
        tokens[tc][ti] = 0;
        tc++;
    }

    if (tc < 4) {
        str_copy(query_result, "Syntax: SELECT cols FROM table [WHERE col op value]", 2048);
        query_result_len = str_len(query_result);
        return;
    }

    if (!str_ieq(tokens[0], "SELECT")) {
        str_copy(query_result, "Expected SELECT", 2048);
        query_result_len = str_len(query_result);
        return;
    }

    // Find FROM position
    int from_pos = -1;
    for (int i = 1; i < tc; i++) {
        if (str_ieq(tokens[i], "FROM")) { from_pos = i; break; }
    }
    if (from_pos < 0 || from_pos + 1 >= tc) {
        str_copy(query_result, "Expected FROM table", 2048);
        query_result_len = str_len(query_result);
        return;
    }

    // Find table
    char* tname = tokens[from_pos + 1];
    Table* t = 0;
    for (int i = 0; i < MAX_TABLES; i++) {
        if (tables[i].in_use && str_ieq(tables[i].name, tname)) {
            t = &tables[i]; break;
        }
    }
    if (!t) {
        str_copy(query_result, "Table not found", 2048);
        query_result_len = str_len(query_result);
        return;
    }

    // Parse selected columns (* = all)
    bool select_all = str_eq(tokens[1], "*");
    int sel_cols[MAX_COLUMNS];
    int sel_count = 0;
    if (select_all) {
        for (int i = 0; i < t->col_count; i++) sel_cols[sel_count++] = i;
    } else {
        // Parse comma-separated column names (simplified: one per token before FROM)
        for (int i = 1; i < from_pos; i++) {
            // Remove trailing comma
            int tl = str_len(tokens[i]);
            if (tl > 0 && tokens[i][tl - 1] == ',') tokens[i][tl - 1] = 0;
            for (int c = 0; c < t->col_count; c++) {
                if (str_ieq(t->columns[c].name, tokens[i])) {
                    sel_cols[sel_count++] = c;
                    break;
                }
            }
        }
    }

    // Parse WHERE clause
    int where_col = -1;
    char where_op = 0;
    char where_val[MAX_CELL] = {0};
    int where_pos = from_pos + 2;
    if (where_pos < tc && str_ieq(tokens[where_pos], "WHERE") && where_pos + 3 < tc) {
        // Find column
        for (int c = 0; c < t->col_count; c++) {
            if (str_ieq(t->columns[c].name, tokens[where_pos + 1])) {
                where_col = c; break;
            }
        }
        if (tokens[where_pos + 2][0] == '=') where_op = '=';
        else if (tokens[where_pos + 2][0] == '>' ) where_op = '>';
        else if (tokens[where_pos + 2][0] == '<') where_op = '<';
        str_copy(where_val, tokens[where_pos + 3], MAX_CELL);
    }

    // Header
    int rpos = 0;
    for (int i = 0; i < sel_count; i++) {
        int c = sel_cols[i];
        int nl = str_len(t->columns[c].name);
        for (int j = 0; j < nl && rpos < 2040; j++)
            query_result[rpos++] = t->columns[c].name[j];
        if (i < sel_count - 1 && rpos < 2040) query_result[rpos++] = '\t';
    }
    if (rpos < 2040) query_result[rpos++] = '\n';
    // Separator
    for (int i = 0; i < sel_count * 10 && rpos < 2040; i++)
        query_result[rpos++] = '-';
    if (rpos < 2040) query_result[rpos++] = '\n';

    // Rows
    int match_count = 0;
    for (int r = 0; r < t->row_count; r++) {
        if (t->rows[r].deleted) continue;

        // Check WHERE
        if (where_col >= 0) {
            bool pass = false;
            if (t->columns[where_col].type == COL_NUMBER) {
                int a = parse_int(t->rows[r].cells[where_col]);
                int b = parse_int(where_val);
                if (where_op == '=' && a == b) pass = true;
                if (where_op == '>' && a > b) pass = true;
                if (where_op == '<' && a < b) pass = true;
            } else {
                int cmp = neo_strcmp(t->rows[r].cells[where_col], where_val);
                if (where_op == '=' && cmp == 0) pass = true;
                if (where_op == '>' && cmp > 0) pass = true;
                if (where_op == '<' && cmp < 0) pass = true;
            }
            if (!pass) continue;
        }

        for (int i = 0; i < sel_count; i++) {
            int c = sel_cols[i];
            int cl = str_len(t->rows[r].cells[c]);
            for (int j = 0; j < cl && rpos < 2040; j++)
                query_result[rpos++] = t->rows[r].cells[c][j];
            if (i < sel_count - 1 && rpos < 2040) query_result[rpos++] = '\t';
        }
        if (rpos < 2040) query_result[rpos++] = '\n';
        match_count++;
    }

    // Footer
    char footer[32];
    ksprintf(footer, 32, "\n%d rows matched.\n", match_count);
    int fl = str_len(footer);
    for (int i = 0; i < fl && rpos < 2047; i++) query_result[rpos++] = footer[i];
    query_result[rpos] = 0;
    query_result_len = rpos;
}

// === Drawing ===

static void draw_title_bar() {
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0, 6);
    neo::display::set_bold(true);
    neo::display::puts(" NeoBase ");
    neo::display::set_bold(false);
    if (filename[0]) neo::display::puts(filename);
    else neo::display::puts("[New Database]");
    if (modified) neo::display::puts(" *");

    // Show table list
    neo::display::puts("  Tables: ");
    for (int i = 0; i < MAX_TABLES; i++) {
        if (tables[i].in_use) {
            if (i == current_table) {
                neo::display::set_bold(true);
                neo::display::putchar('[');
                neo::display::puts(tables[i].name);
                neo::display::putchar(']');
                neo::display::set_bold(false);
            } else {
                neo::display::puts(tables[i].name);
            }
            neo::display::putchar(' ');
        }
    }
    neo::display::clear_eol();
}

static void draw_menu() {
    neo::display::set_cursor(0, 1);
    neo::display::set_color(7, 4);
    neo::display::puts(" F1:New Tbl F2:Save F3:Load F4:Add Row F5:Del Row F6:Sort F7:Form F8:Query Tab:Tbl ESC:Quit ");
    neo::display::clear_eol();
}

static void draw_browser() {
    Table* t = cur_table();
    int start_y = 2;

    if (!t) {
        neo::display::set_cursor(2, 4);
        neo::display::set_color(8, 0);
        neo::display::puts("No table selected. Press F1 to create a table.");
        for (int y = 5; y < screen_h - 1; y++) {
            neo::display::set_cursor(0, y);
            neo::display::clear_eol();
        }
        return;
    }

    // Column headers
    neo::display::set_cursor(0, start_y);
    neo::display::set_color(0, 7);
    char rh[6];
    ksprintf(rh, 6, "%4s ", "#");
    neo::display::puts(rh);

    int x = 5;
    for (int c = scroll_col; c < t->col_count && x < screen_w; c++) {
        int w = t->columns[c].width;
        bool is_cur = (c == cursor_col);
        if (is_cur) neo::display::set_color(0x000000, 0xFFFF88);
        else neo::display::set_color(0, 7);

        const char* cn = t->columns[c].name;
        int nl = str_len(cn);
        neo::display::putchar(' ');
        for (int i = 0; i < w - 1; i++) {
            if (i < nl) neo::display::putchar(cn[i]);
            else neo::display::putchar(' ');
        }
        x += w;
    }
    neo::display::clear_eol();

    // Type indicator row
    neo::display::set_cursor(0, start_y + 1);
    neo::display::set_color(7, 4);
    neo::display::puts("     ");
    x = 5;
    const char* type_names[] = { "text", "num", "date" };
    for (int c = scroll_col; c < t->col_count && x < screen_w; c++) {
        int w = t->columns[c].width;
        neo::display::putchar(' ');
        neo::display::puts(type_names[t->columns[c].type]);
        int tl = str_len(type_names[t->columns[c].type]);
        for (int i = tl + 1; i < w; i++) neo::display::putchar(' ');
        x += w;
    }
    neo::display::clear_eol();

    // Data rows
    int data_start = start_y + 2;
    int max_vis = screen_h - data_start - 1;
    for (int vr = 0; vr < max_vis; vr++) {
        int r = scroll_row + vr;
        int sy = data_start + vr;
        neo::display::set_cursor(0, sy);

        if (r >= t->row_count) {
            neo::display::set_color(8, 0);
            neo::display::clear_eol();
            continue;
        }

        if (t->rows[r].deleted) {
            neo::display::set_color(4, 0);
            ksprintf(rh, 6, "%4d ", r + 1);
            neo::display::puts(rh);
            neo::display::puts("[DELETED]");
            neo::display::clear_eol();
            continue;
        }

        bool is_cur_row = (r == cursor_row);
        neo::display::set_color(is_cur_row ? 0xFFFF88 : 0x7777AA, 0x111122);
        ksprintf(rh, 6, "%4d ", r + 1);
        neo::display::puts(rh);

        x = 5;
        for (int c = scroll_col; c < t->col_count && x < screen_w; c++) {
            int w = t->columns[c].width;
            bool is_active = (is_cur_row && c == cursor_col);

            if (is_active && editing) {
                neo::display::set_color(14, 4);
            } else if (is_active) {
                neo::display::set_color(0x000000, 0xFFFF88);
            } else if (is_cur_row) {
                neo::display::set_color(7, 4);
            } else {
                neo::display::set_color(7, 0);
            }

            const char* val = (is_active && editing) ? edit_buf : t->rows[r].cells[c];
            int vl = str_len(val);
            neo::display::putchar(' ');
            for (int i = 0; i < w - 1; i++) {
                if (i < vl) neo::display::putchar(val[i]);
                else neo::display::putchar(' ');
            }
            x += w;
        }
        neo::display::clear_eol();
    }

    // Status
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0, 10);
    char status[128];
    ksprintf(status, 128, " Table: %s | Rows: %d | Col: %s | Row: %d ",
             t->name, t->row_count,
             cursor_col < t->col_count ? t->columns[cursor_col].name : "?",
             cursor_row + 1);
    neo::display::puts(status);
    neo::display::clear_eol();
}

static void draw_form() {
    Table* t = cur_table();
    if (!t) return;

    neo::display::set_cursor(0, 2);
    neo::display::set_color(11, 0);
    neo::display::set_bold(true);
    char hdr[64];
    ksprintf(hdr, 64, " Record %d of %d ", cursor_row + 1, t->row_count);
    neo::display::puts(hdr);
    neo::display::set_bold(false);
    neo::display::clear_eol();

    neo::display::set_cursor(0, 3);
    neo::display::set_color(9, 0);
    for (int i = 0; i < screen_w; i++) neo::display::putchar('-');

    if (cursor_row >= t->row_count) {
        neo::display::set_cursor(4, 5);
        neo::display::set_color(8, 0);
        neo::display::puts("No records. Press F4 to add one.");
        return;
    }

    Row* row = &t->rows[cursor_row];
    for (int c = 0; c < t->col_count; c++) {
        int y = 4 + c * 2;
        if (y >= screen_h - 2) break;

        bool active = (c == cursor_col);
        neo::display::set_cursor(4, y);
        neo::display::set_color(7, 0);
        neo::display::puts(t->columns[c].name);
        neo::display::puts(": ");

        if (active && editing) {
            neo::display::set_color(14, 4);
            neo::display::puts(edit_buf);
        } else {
            neo::display::set_color(active ? 0xFFFF88 : 0xDDDDDD, 0x111122);
            neo::display::puts(row->cells[c]);
        }
        neo::display::clear_eol();

        // Type hint
        neo::display::set_cursor(4, y + 1);
        neo::display::set_color(8, 0);
        const char* types[] = { "(text)", "(number)", "(date)" };
        neo::display::puts("  ");
        neo::display::puts(types[t->columns[c].type]);
        neo::display::clear_eol();
    }

    // Clear remaining
    int last_y = 4 + t->col_count * 2;
    for (int y = last_y; y < screen_h - 1; y++) {
        neo::display::set_cursor(0, y);
        neo::display::set_color(0, 0);
        neo::display::clear_eol();
    }
}

static void draw_query() {
    neo::display::set_cursor(0, 2);
    neo::display::set_color(11, 0);
    neo::display::set_bold(true);
    neo::display::puts(" SQL Query ");
    neo::display::set_bold(false);
    neo::display::clear_eol();

    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 4);
    neo::display::puts("> ");
    neo::display::puts(query_buf);
    neo::display::clear_eol();

    neo::display::set_cursor(0, 4);
    neo::display::set_color(9, 0);
    for (int i = 0; i < screen_w; i++) neo::display::putchar('-');

    // Display results
    int y = 5;
    int ri = 0;
    while (ri < query_result_len && y < screen_h - 1) {
        neo::display::set_cursor(0, y);
        neo::display::set_color(7, 0);
        while (ri < query_result_len && query_result[ri] != '\n') {
            if (query_result[ri] == '\t') {
                neo::display::puts("  | ");
            } else {
                neo::display::putchar(query_result[ri]);
            }
            ri++;
        }
        neo::display::clear_eol();
        if (ri < query_result_len) ri++; // skip newline
        y++;
    }
    for (; y < screen_h - 1; y++) {
        neo::display::set_cursor(0, y);
        neo::display::set_color(0, 0);
        neo::display::clear_eol();
    }
}

static void draw_screen() {
    draw_title_bar();
    draw_menu();
    switch (view_mode) {
        case VIEW_BROWSER: draw_browser(); break;
        case VIEW_FORM: draw_form(); break;
        case VIEW_QUERY: draw_query(); break;
    }
}

static void start_edit() {
    Table* t = cur_table();
    if (!t || cursor_row >= t->row_count) return;
    editing = true;
    str_copy(edit_buf, t->rows[cursor_row].cells[cursor_col], MAX_CELL);
    edit_pos = str_len(edit_buf);
}

static void finish_edit() {
    Table* t = cur_table();
    if (!t || cursor_row >= t->row_count) return;
    editing = false;
    str_copy(t->rows[cursor_row].cells[cursor_col], edit_buf, MAX_CELL);
    modified = true;
}

static void ensure_visible() {
    Table* t = cur_table();
    if (!t) return;
    int data_start = 4;
    int max_vis = screen_h - data_start - 1;
    if (cursor_row < scroll_row) scroll_row = cursor_row;
    if (cursor_row >= scroll_row + max_vis) scroll_row = cursor_row - max_vis + 1;
    if (cursor_col < scroll_col) scroll_col = cursor_col;
    // Simple: ensure at least current col visible
    if (cursor_col > scroll_col + 5) scroll_col = cursor_col - 5;
}

static void switch_table() {
    // Cycle to next table
    int start = current_table;
    int next = (current_table + 1) % MAX_TABLES;
    while (next != start) {
        if (tables[next].in_use) { current_table = next; cursor_row = 0; cursor_col = 0; return; }
        next = (next + 1) % MAX_TABLES;
    }
}

static void do_save() {
    if (filename[0] == 0) {
        prompt_input("Save as: ", filename, 64);
        if (filename[0] == 0) return;
    }
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, filename, 1) != 0) return;

    for (int t = 0; t < MAX_TABLES; t++) {
        if (!tables[t].in_use) continue;
        char hdr[128];
        ksprintf(hdr, 128, "TABLE\t%s\t%d\t%d\n", tables[t].name, tables[t].col_count, tables[t].row_count);
        neo::filesystem::write(fh, hdr, str_len(hdr));

        // Column defs
        for (int c = 0; c < tables[t].col_count; c++) {
            char cdef[64];
            ksprintf(cdef, 64, "COL\t%s\t%d\t%d\n",
                     tables[t].columns[c].name,
                     (int)tables[t].columns[c].type,
                     tables[t].columns[c].width);
            neo::filesystem::write(fh, cdef, str_len(cdef));
        }

        // Rows
        for (int r = 0; r < tables[t].row_count; r++) {
            if (tables[t].rows[r].deleted) continue;
            char row_hdr[] = "ROW\t";
            neo::filesystem::write(fh, row_hdr, 4);
            for (int c = 0; c < tables[t].col_count; c++) {
                neo::filesystem::write(fh, tables[t].rows[r].cells[c],
                                       str_len(tables[t].rows[r].cells[c]));
                char sep = (c < tables[t].col_count - 1) ? '\t' : '\n';
                neo::filesystem::write(fh, &sep, 1);
            }
        }
    }
    neo::filesystem::close(fh);
    modified = false;
}

static void do_load() {
    char fname[64];
    prompt_input("Load file: ", fname, 64);
    if (fname[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, fname, 0) != 0) return;

    init_tables();
    str_copy(filename, fname, 64);

    char buf[4096];
    int n = neo::filesystem::read(fh, buf, 4096);
    neo::filesystem::close(fh);
    if (n <= 0) return;
    buf[n] = 0;

    int p = 0;
    int ct = -1;

    while (p < n) {
        // Read line
        char line[INODE_SIZE];
        int li = 0;
        while (p < n && buf[p] != '\n' && li < 255) line[li++] = buf[p++];
        line[li] = 0;
        if (p < n) p++;

        // Tokenize by tab
        char toks[MAX_COLUMNS + 2][MAX_CELL];
        int ntoks = 0;
        int tp = 0;
        while (tp < li && ntoks < MAX_COLUMNS + 2) {
            int ti = 0;
            while (tp < li && line[tp] != '\t' && ti < MAX_CELL - 1) toks[ntoks][ti++] = line[tp++];
            toks[ntoks][ti] = 0;
            ntoks++;
            if (tp < li) tp++;
        }

        if (ntoks > 0 && str_eq(toks[0], "TABLE") && ntoks >= 4) {
            ct++;
            if (ct >= MAX_TABLES) break;
            str_copy(tables[ct].name, toks[1], MAX_NAME);
            tables[ct].col_count = parse_int(toks[2]);
            tables[ct].row_count = 0;
            tables[ct].in_use = true;
        } else if (ntoks > 0 && str_eq(toks[0], "COL") && ntoks >= 4 && ct >= 0) {
            int ci = 0;
            // Find next empty column slot
            for (int c = 0; c < tables[ct].col_count; c++) {
                if (tables[ct].columns[c].name[0] == 0) { ci = c; break; }
            }
            str_copy(tables[ct].columns[ci].name, toks[1], MAX_NAME);
            tables[ct].columns[ci].type = (ColType)parse_int(toks[2]);
            tables[ct].columns[ci].width = parse_int(toks[3]);
        } else if (ntoks > 0 && str_eq(toks[0], "ROW") && ct >= 0) {
            int r = tables[ct].row_count;
            if (r < MAX_ROWS) {
                tables[ct].rows[r].deleted = false;
                for (int c = 0; c < tables[ct].col_count && c + 1 < ntoks; c++) {
                    str_copy(tables[ct].rows[r].cells[c], toks[c + 1], MAX_CELL);
                }
                tables[ct].row_count++;
            }
        }
    }
    if (ct >= 0) current_table = 0;
}

static void handle_key(int scancode) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(scancode, shift);
    Table* t = cur_table();

    if (editing) {
        if (ch == '\n' || ch == '\r') { finish_edit(); return; }
        if (scancode == 0x01) { editing = false; return; }
        if ((ch == 8 || scancode == 0x0E) && edit_pos > 0) {
            edit_pos--; edit_buf[edit_pos] = 0; return;
        }
        if (ch >= ' ' && ch < 127 && edit_pos < MAX_CELL - 1) {
            edit_buf[edit_pos++] = ch; edit_buf[edit_pos] = 0; return;
        }
        return;
    }

    if (view_mode == VIEW_QUERY) {
        if (scancode == 0x01) { view_mode = VIEW_BROWSER; neo::display::clear(); return; }
        prompt_input("SQL> ", query_buf, INODE_SIZE);
        if (query_buf[0]) execute_query();
        return;
    }

    switch (scancode) {
        case 0x01: running = false; return;
        case 0x3B: create_table(); neo::display::clear(); return; // F1
        case 0x3C: do_save(); return; // F2
        case 0x3D: do_load(); neo::display::clear(); return; // F3
        case 0x3E: add_row(); return; // F4
        case 0x3F: delete_row(); return; // F5
        case 0x40: sort_table(); return; // F6
        case 0x41: // F7 toggle form
            view_mode = (view_mode == VIEW_FORM) ? VIEW_BROWSER : VIEW_FORM;
            neo::display::clear();
            return;
        case 0x42: // F8 query
            view_mode = VIEW_QUERY;
            neo::display::clear();
            prompt_input("SQL> ", query_buf, INODE_SIZE);
            if (query_buf[0]) execute_query();
            return;
        case 0x0F: switch_table(); return; // Tab

        case 0x48: // Up
            if (view_mode == VIEW_FORM) {
                if (cursor_col > 0) cursor_col--;
            } else {
                if (cursor_row > 0) cursor_row--;
            }
            ensure_visible();
            return;
        case 0x50: // Down
            if (t) {
                if (view_mode == VIEW_FORM) {
                    if (cursor_col < t->col_count - 1) cursor_col++;
                } else {
                    if (cursor_row < t->row_count - 1) cursor_row++;
                }
            }
            ensure_visible();
            return;
        case 0x4B: // Left
            if (view_mode == VIEW_BROWSER && cursor_col > 0) cursor_col--;
            else if (view_mode == VIEW_FORM && cursor_row > 0) cursor_row--;
            ensure_visible();
            return;
        case 0x4D: // Right
            if (t) {
                if (view_mode == VIEW_BROWSER && cursor_col < t->col_count - 1) cursor_col++;
                else if (view_mode == VIEW_FORM && cursor_row < t->row_count - 1) cursor_row++;
            }
            ensure_visible();
            return;
        case 0x49: // PgUp
            cursor_row = (cursor_row > 20) ? cursor_row - 20 : 0;
            ensure_visible();
            return;
        case 0x51: // PgDn
            if (t) cursor_row = (cursor_row + 20 < t->row_count) ? cursor_row + 20 : t->row_count - 1;
            if (cursor_row < 0) cursor_row = 0;
            ensure_visible();
            return;
    }

    if (ch == '\n' || ch == '\r') {
        start_edit();
        return;
    }
    if (ch >= ' ' && ch < 127 && t && cursor_row < t->row_count) {
        editing = true;
        edit_buf[0] = ch;
        edit_buf[1] = 0;
        edit_pos = 1;
    }
}

} // namespace neobase

extern "C" void app_main(int argc, char** argv) {
    using namespace neobase;

    screen_w = neo::display::get_width();
    screen_h = neo::display::get_height();
    running = true;

    init_tables();

    if (argc > 1) {
        str_copy(filename, argv[1], 64);
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, 0) == 0) {
            neo::filesystem::close(fh);
            do_load();
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
