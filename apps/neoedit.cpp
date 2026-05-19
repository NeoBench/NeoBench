#include "../include/neobench.h"
#include "../lib/string.h"

// NeoEdit - Text Editor with Syntax Highlighting
// Gap buffer implementation, multi-buffer, find/replace, goto line

namespace neoedit {

static const int MAX_BUFSIZE = 64 * 1024;
static const int GAP_SIZE = INODE_SIZE;
static const int MAX_BUFFERS = 8;
static const int MAX_LINE_LEN = INODE_SIZE;
static const int TAB_WIDTH = 4;

// C/C++ keywords for syntax highlighting
static const char* keywords[] = {
    "if", "else", "while", "for", "do", "switch", "case", "break",
    "continue", "return", "int", "void", "char", "short", "long",
    "unsigned", "signed", "float", "double", "struct", "union",
    "enum", "typedef", "static", "extern", "const", "volatile",
    "auto", "register", "sizeof", "goto", "default", "include",
    "define", "ifdef", "ifndef", "endif", "pragma", "class",
    "public", "private", "protected", "virtual", "namespace",
    "template", "typename", "bool", "true", "false", "nullptr",
    nullptr
};

struct GapBuffer {
    char* data;
    int capacity;
    int gap_start;
    int gap_end;
    bool modified;
    char filename[128];

    void init() {
        capacity = MAX_BUFSIZE;
        data = (char*)neo::mem::alloc(capacity);
        if (data) {
            neo_memset(data, 0, capacity);
        }
        gap_start = 0;
        gap_end = GAP_SIZE;
        modified = false;
        filename[0] = '\0';
    }

    void destroy() {
        if (data) {
            neo::mem::free(data);
            data = nullptr;
        }
    }

    int length() const {
        return capacity - (gap_end - gap_start);
    }

    char char_at(int pos) const {
        if (pos < 0 || pos >= length()) return '\0';
        if (pos < gap_start) return data[pos];
        return data[pos + (gap_end - gap_start)];
    }

    void move_gap(int pos) {
        if (pos == gap_start) return;
        int gap_len = gap_end - gap_start;
        if (pos < gap_start) {
            int move_count = gap_start - pos;
            for (int i = move_count - 1; i >= 0; i--) {
                data[gap_end - move_count + i] = data[pos + i];
            }
            gap_start = pos;
            gap_end = pos + gap_len;
        } else {
            int move_count = pos - gap_start;
            for (int i = 0; i < move_count; i++) {
                data[gap_start + i] = data[gap_end + i];
            }
            gap_start = pos;
            gap_end = pos + gap_len;
        }
    }

    void insert_char(int pos, char c) {
        if (gap_end - gap_start < 2) return; // no room
        move_gap(pos);
        data[gap_start] = c;
        gap_start++;
        modified = true;
    }

    void delete_char(int pos) {
        if (pos < 0 || pos >= length()) return;
        move_gap(pos);
        gap_end++;
        modified = true;
    }

    void delete_backward(int pos) {
        if (pos <= 0) return;
        move_gap(pos);
        gap_start--;
        modified = true;
    }

    void get_line(int line_num, char* out, int max_len) const {
        int cur_line = 0;
        int start = 0;
        int len = length();
        for (int i = 0; i < len; i++) {
            if (cur_line == line_num) {
                start = i;
                break;
            }
            if (char_at(i) == '\n') cur_line++;
            if (cur_line == line_num) {
                start = i + 1;
                break;
            }
        }
        int j = 0;
        for (int i = start; i < len && j < max_len - 1; i++) {
            char c = char_at(i);
            if (c == '\n') break;
            out[j++] = c;
        }
        out[j] = '\0';
    }

    int count_lines() const {
        int lines = 1;
        int len = length();
        for (int i = 0; i < len; i++) {
            if (char_at(i) == '\n') lines++;
        }
        return lines;
    }

    int pos_from_line_col(int line, int col) const {
        int cur_line = 0;
        int len = length();
        for (int i = 0; i < len; i++) {
            if (cur_line == line) {
                int c = 0;
                while (i + c < len && char_at(i + c) != '\n' && c < col) c++;
                return i + c;
            }
            if (char_at(i) == '\n') cur_line++;
        }
        return len;
    }

    void line_col_from_pos(int pos, int& line, int& col) const {
        line = 0;
        col = 0;
        for (int i = 0; i < pos && i < length(); i++) {
            if (char_at(i) == '\n') {
                line++;
                col = 0;
            } else {
                col++;
            }
        }
    }
};

struct Editor {
    GapBuffer buffers[MAX_BUFFERS];
    int num_buffers;
    int active_buf;
    int cursor_pos;
    int cursor_line;
    int cursor_col;
    int scroll_y;
    int screen_w;
    int screen_h;
    bool running;
    bool show_line_numbers;
    int line_num_width;

    // Find/replace
    char find_str[64];
    char replace_str[64];
    bool in_find_mode;
    bool in_replace_mode;
    bool in_goto_mode;
    char input_buf[128];
    int input_pos;
    char status_msg[128];
    int status_timer;

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        num_buffers = 1;
        active_buf = 0;
        buffers[0].init();
        neo_strcpy(buffers[0].filename, "[untitled]");
        cursor_pos = 0;
        cursor_line = 0;
        cursor_col = 0;
        scroll_y = 0;
        running = true;
        show_line_numbers = true;
        line_num_width = 5;
        find_str[0] = '\0';
        replace_str[0] = '\0';
        in_find_mode = false;
        in_replace_mode = false;
        in_goto_mode = false;
        input_buf[0] = '\0';
        input_pos = 0;
        status_msg[0] = '\0';
        status_timer = 0;
    }

    void destroy() {
        for (int i = 0; i < num_buffers; i++) {
            buffers[i].destroy();
        }
    }

    GapBuffer& buf() { return buffers[active_buf]; }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 100;
    }

    bool is_keyword(const char* word, int len) {
        for (int i = 0; keywords[i]; i++) {
            int klen = neo_strlen(keywords[i]);
            if (klen == len && neo_strncmp(word, keywords[i], len) == 0) return true;
        }
        return false;
    }

    bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool is_alnum(char c) {
        return is_alpha(c) || (c >= '0' && c <= '9');
    }

    bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    void render_line_syntax(const char* line, int y, int text_x) {
        int x = text_x;
        int i = 0;
        int len = neo_strlen(line);

        while (i < len && x < screen_w) {
            // Comments
            if (line[i] == '/' && i + 1 < len && line[i + 1] == '/') {
                neo::display::set_fg(6); // cyan
                while (i < len && x < screen_w) {
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[i++]);
                }
                break;
            }
            // Strings
            if (line[i] == '"') {
                neo::display::set_fg(5); // magenta
                neo::display::set_cursor(x++, y);
                neo::display::putchar(line[i++]);
                while (i < len && line[i] != '"' && x < screen_w) {
                    if (line[i] == '\\' && i + 1 < len) {
                        neo::display::set_cursor(x++, y);
                        neo::display::putchar(line[i++]);
                    }
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[i++]);
                }
                if (i < len && x < screen_w) {
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[i++]);
                }
                neo::display::set_fg(7);
                continue;
            }
            // Character literals
            if (line[i] == '\'') {
                neo::display::set_fg(5);
                neo::display::set_cursor(x++, y);
                neo::display::putchar(line[i++]);
                while (i < len && line[i] != '\'' && x < screen_w) {
                    if (line[i] == '\\') { neo::display::set_cursor(x++, y); neo::display::putchar(line[i++]); }
                    if (i < len) { neo::display::set_cursor(x++, y); neo::display::putchar(line[i++]); }
                }
                if (i < len && x < screen_w) { neo::display::set_cursor(x++, y); neo::display::putchar(line[i++]); }
                neo::display::set_fg(7);
                continue;
            }
            // Numbers
            if (is_digit(line[i]) || (line[i] == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X'))) {
                neo::display::set_fg(3); // yellow
                while (i < len && (is_alnum(line[i]) || line[i] == '.') && x < screen_w) {
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[i++]);
                }
                neo::display::set_fg(7);
                continue;
            }
            // Keywords / identifiers
            if (is_alpha(line[i])) {
                int start = i;
                while (i < len && is_alnum(line[i])) i++;
                int wlen = i - start;
                if (is_keyword(line + start, wlen)) {
                    neo::display::set_fg(2); // green for keywords
                } else {
                    neo::display::set_fg(7);
                }
                for (int j = start; j < i && x < screen_w; j++) {
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[j]);
                }
                neo::display::set_fg(7);
                continue;
            }
            // Preprocessor
            if (line[i] == '#' && i == 0) {
                neo::display::set_fg(4); // blue
                while (i < len && x < screen_w) {
                    neo::display::set_cursor(x++, y);
                    neo::display::putchar(line[i++]);
                }
                neo::display::set_fg(7);
                continue;
            }
            // Operators
            if (line[i] == '{' || line[i] == '}' || line[i] == '(' || line[i] == ')' ||
                line[i] == '[' || line[i] == ']') {
                neo::display::set_fg(6);
                neo::display::set_cursor(x++, y);
                neo::display::putchar(line[i++]);
                neo::display::set_fg(7);
                continue;
            }
            // Default
            neo::display::set_fg(7);
            neo::display::set_cursor(x++, y);
            neo::display::putchar(line[i++]);
        }
    }

    void render() {
        neo::display::clear();
        int text_area_h = screen_h - 3; // title bar + status + help
        int text_x = show_line_numbers ? line_num_width : 0;

        // Title bar
        neo::display::set_color(0, 3); // black on yellow
        neo::display::set_cursor(0, 0);
        char title[128];
        ksprintf(title, sizeof(title), " NeoEdit - %s%s ", buf().filename, buf().modified ? " [+]" : "");
        neo::display::puts(title);
        // Buffer tabs
        for (int i = neo_strlen(title); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }
        int tx = screen_w - 20;
        for (int i = 0; i < num_buffers; i++) {
            if (i == active_buf) neo::display::set_color(0, 7);
            else neo::display::set_color(7, 1);
            char bnum[4];
            ksprintf(bnum, sizeof(bnum), " %d ", i + 1);
            neo::display::set_cursor(tx, 0);
            neo::display::puts(bnum);
            tx += 3;
        }

        // Text area
        neo::display::set_color(7, 0);
        int total_lines = buf().count_lines();
        for (int y = 0; y < text_area_h; y++) {
            int line = scroll_y + y;
            int sy = y + 1;
            if (line < total_lines) {
                // Line numbers
                if (show_line_numbers) {
                    neo::display::set_color(6, 0);
                    char lnum[8];
                    ksprintf(lnum, sizeof(lnum), "%4d", line + 1);
                    neo::display::set_cursor(0, sy);
                    neo::display::puts(lnum);
                    neo::display::set_cursor(line_num_width - 1, sy);
                    neo::display::putchar('|');
                }
                // Line content with syntax highlighting
                char linebuf[MAX_LINE_LEN];
                buf().get_line(line, linebuf, MAX_LINE_LEN);
                neo::display::set_color(7, 0);
                render_line_syntax(linebuf, sy, text_x);
            }
        }

        // Cursor
        int cur_screen_y = cursor_line - scroll_y + 1;
        int cur_screen_x = text_x + cursor_col;
        if (cur_screen_y >= 1 && cur_screen_y < screen_h - 2) {
            neo::display::set_cursor(cur_screen_x, cur_screen_y);
        }

        // Status bar
        neo::display::set_color(0, 2); // black on green
        neo::display::set_cursor(0, screen_h - 2);
        char status[128];
        ksprintf(status, sizeof(status), " L:%d/%d C:%d | %s | %d bytes ",
                 cursor_line + 1, total_lines, cursor_col + 1,
                 buf().modified ? "MOD" : "---", buf().length());
        neo::display::puts(status);
        for (int i = neo_strlen(status); i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 2);
            neo::display::putchar(' ');
        }
        if (status_msg[0] && status_timer > 0) {
            neo::display::set_cursor(screen_w - neo_strlen(status_msg) - 2, screen_h - 2);
            neo::display::putchar(' ');
            neo::display::puts(status_msg);
        }

        // Help / input bar
        neo::display::set_color(7, 0);
        neo::display::set_cursor(0, screen_h - 1);
        if (in_find_mode) {
            neo::display::puts("Find: ");
            neo::display::puts(input_buf);
            neo::display::putchar('_');
        } else if (in_replace_mode) {
            neo::display::puts("Replace with: ");
            neo::display::puts(input_buf);
            neo::display::putchar('_');
        } else if (in_goto_mode) {
            neo::display::puts("Goto line: ");
            neo::display::puts(input_buf);
            neo::display::putchar('_');
        } else {
            neo::display::set_fg(6);
            neo::display::puts("^Q:Quit ^S:Save ^F:Find ^R:Replace ^G:Goto ^N:New ^B:NextBuf");
        }
    }

    void update_cursor_info() {
        buf().line_col_from_pos(cursor_pos, cursor_line, cursor_col);
        // Scroll into view
        int text_area_h = screen_h - 3;
        if (cursor_line < scroll_y) scroll_y = cursor_line;
        if (cursor_line >= scroll_y + text_area_h) scroll_y = cursor_line - text_area_h + 1;
    }

    void move_up() {
        if (cursor_line > 0) {
            int target_col = cursor_col;
            cursor_pos = buf().pos_from_line_col(cursor_line - 1, target_col);
            update_cursor_info();
        }
    }

    void move_down() {
        if (cursor_line < buf().count_lines() - 1) {
            int target_col = cursor_col;
            cursor_pos = buf().pos_from_line_col(cursor_line + 1, target_col);
            update_cursor_info();
        }
    }

    void move_left() {
        if (cursor_pos > 0) {
            cursor_pos--;
            update_cursor_info();
        }
    }

    void move_right() {
        if (cursor_pos < buf().length()) {
            cursor_pos++;
            update_cursor_info();
        }
    }

    void move_home() {
        cursor_pos = buf().pos_from_line_col(cursor_line, 0);
        update_cursor_info();
    }

    void move_end() {
        cursor_pos = buf().pos_from_line_col(cursor_line, 9999);
        update_cursor_info();
    }

    void insert_char(char c) {
        buf().insert_char(cursor_pos, c);
        cursor_pos++;
        update_cursor_info();
    }

    void insert_tab() {
        for (int i = 0; i < TAB_WIDTH; i++) insert_char(' ');
    }

    void delete_forward() {
        if (cursor_pos < buf().length()) {
            buf().delete_char(cursor_pos);
            update_cursor_info();
        }
    }

    void delete_backward() {
        if (cursor_pos > 0) {
            buf().delete_backward(cursor_pos);
            cursor_pos--;
            update_cursor_info();
        }
    }

    void new_buffer() {
        if (num_buffers < MAX_BUFFERS) {
            buffers[num_buffers].init();
            ksprintf(buffers[num_buffers].filename, 128, "[untitled-%d]", num_buffers + 1);
            active_buf = num_buffers;
            num_buffers++;
            cursor_pos = 0;
            scroll_y = 0;
            update_cursor_info();
            set_status("New buffer created");
        }
    }

    void next_buffer() {
        if (num_buffers > 1) {
            active_buf = (active_buf + 1) % num_buffers;
            cursor_pos = 0;
            scroll_y = 0;
            update_cursor_info();
        }
    }

    void save_file() {
        if (buf().filename[0] == '[') {
            set_status("No filename set");
            return;
        }
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, buf().filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
            int len = buf().length();
            char tmp[512];
            int written = 0;
            while (written < len) {
                int chunk = 0;
                while (chunk < 511 && written + chunk < len) {
                    tmp[chunk] = buf().char_at(written + chunk);
                    chunk++;
                }
                tmp[chunk] = '\0';
                neo::filesystem::write(fh, tmp, chunk);
                written += chunk;
            }
            neo::filesystem::close(fh);
            buf().modified = false;
            set_status("File saved");
        } else {
            set_status("Save failed!");
        }
    }

    void load_file(const char* path) {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) == 0) {
            char tmp[512];
            int bytes;
            cursor_pos = 0;
            while ((bytes = neo::filesystem::read(fh, tmp, 511)) > 0) {
                for (int i = 0; i < bytes; i++) {
                    buf().insert_char(cursor_pos, tmp[i]);
                    cursor_pos++;
                }
            }
            neo::filesystem::close(fh);
            neo_strcpy(buf().filename, path);
            buf().modified = false;
            cursor_pos = 0;
            scroll_y = 0;
            update_cursor_info();
            set_status("File loaded");
        }
    }

    void find_next() {
        int flen = neo_strlen(find_str);
        if (flen == 0) return;
        int len = buf().length();
        for (int i = cursor_pos + 1; i <= len - flen; i++) {
            bool match = true;
            for (int j = 0; j < flen; j++) {
                if (buf().char_at(i + j) != find_str[j]) { match = false; break; }
            }
            if (match) {
                cursor_pos = i;
                update_cursor_info();
                set_status("Found");
                return;
            }
        }
        // Wrap around
        for (int i = 0; i < cursor_pos && i <= len - flen; i++) {
            bool match = true;
            for (int j = 0; j < flen; j++) {
                if (buf().char_at(i + j) != find_str[j]) { match = false; break; }
            }
            if (match) {
                cursor_pos = i;
                update_cursor_info();
                set_status("Found (wrapped)");
                return;
            }
        }
        set_status("Not found");
    }

    void do_replace() {
        int flen = neo_strlen(find_str);
        int rlen = neo_strlen(replace_str);
        if (flen == 0) return;
        // Check if current pos matches
        bool match = true;
        for (int j = 0; j < flen; j++) {
            if (buf().char_at(cursor_pos + j) != find_str[j]) { match = false; break; }
        }
        if (match) {
            for (int j = 0; j < flen; j++) buf().delete_char(cursor_pos);
            for (int j = 0; j < rlen; j++) {
                buf().insert_char(cursor_pos + j, replace_str[j]);
            }
            cursor_pos += rlen;
            update_cursor_info();
            set_status("Replaced");
        } else {
            find_next();
        }
    }

    void goto_line(int line) {
        if (line < 1) line = 1;
        int total = buf().count_lines();
        if (line > total) line = total;
        cursor_pos = buf().pos_from_line_col(line - 1, 0);
        update_cursor_info();
    }

    void handle_input_mode(unsigned char sc) {
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        if (sc == 0x44 || ch == 27) { // Escape
            in_find_mode = false;
            in_replace_mode = false;
            in_goto_mode = false;
            return;
        }
        if (sc == 0x44 && ch == '\n') ch = '\n'; // Enter

        if (ch == '\n' || ch == '\r' || sc == 0x44) {
            if (in_find_mode) {
                neo_strcpy(find_str, input_buf);
                in_find_mode = false;
                find_next();
            } else if (in_replace_mode) {
                neo_strcpy(replace_str, input_buf);
                in_replace_mode = false;
                do_replace();
            } else if (in_goto_mode) {
                int line = 0;
                for (int i = 0; input_buf[i]; i++) {
                    if (input_buf[i] >= '0' && input_buf[i] <= '9')
                        line = line * 10 + (input_buf[i] - '0');
                }
                in_goto_mode = false;
                goto_line(line);
            }
            return;
        }
        if (sc == 0x41 || ch == 8) { // Backspace
            if (input_pos > 0) input_buf[--input_pos] = '\0';
            return;
        }
        if (ch >= 32 && ch < 127 && input_pos < 126) {
            input_buf[input_pos++] = ch;
            input_buf[input_pos] = '\0';
        }
    }

    void handle_key(unsigned char sc) {
        if (in_find_mode || in_replace_mode || in_goto_mode) {
            handle_input_mode(sc);
            return;
        }

        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        // Arrow keys (Amiga scancodes)
        if (sc == 0x4C) { move_up(); return; }
        if (sc == 0x4D) { move_down(); return; }
        if (sc == 0x4F) { move_left(); return; }
        if (sc == 0x50) { move_right(); return; }

        // Ctrl combos (check raw scancode for letter keys)
        // Ctrl+Q = quit (scancode for Q = 0x10)
        if (sc == 0x10 && !shift) { running = false; return; }
        // Ctrl+S = save (scancode for S = 0x21)
        if (sc == 0x21 && !shift) { save_file(); return; }
        // Ctrl+F = find
        if (sc == 0x23 && !shift) {
            in_find_mode = true;
            input_buf[0] = '\0';
            input_pos = 0;
            return;
        }
        // Ctrl+R = replace
        if (sc == 0x13 && !shift) {
            in_replace_mode = true;
            input_buf[0] = '\0';
            input_pos = 0;
            return;
        }
        // Ctrl+G = goto
        if (sc == 0x24 && !shift) {
            in_goto_mode = true;
            input_buf[0] = '\0';
            input_pos = 0;
            return;
        }
        // Ctrl+N = new buffer
        if (sc == 0x36 && !shift) { new_buffer(); return; }
        // Ctrl+B = next buffer
        if (sc == 0x35 && !shift) { next_buffer(); return; }

        // Backspace
        if (sc == 0x41) { delete_backward(); return; }
        // Delete
        if (sc == 0x46) { delete_forward(); return; }
        // Return / Enter
        if (sc == 0x44 || ch == '\n' || ch == '\r') { insert_char('\n'); return; }
        // Tab
        if (sc == 0x42) { insert_tab(); return; }

        // Regular character
        if (ch >= 32 && ch < 127) {
            insert_char(ch);
        }
    }

    void run(int argc, char** argv) {
        init();

        // Load file from args
        if (argc > 1) {
            load_file(argv[1]);
        }

        while (running) {
            render();
            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            while (!neo::keyboard::key_available()) {
                neo::timer::delay_ms(10);
            }
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc & 0x80) continue; // key up event
            handle_key(sc);
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoEdit: Goodbye.\n");
        destroy();
    }
};

} // namespace neoedit

extern "C" void app_main(int argc, char** argv) {
    neoedit::Editor editor;
    editor.run(argc, argv);
}
