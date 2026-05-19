#include "../include/neobench.h"
#include "../lib/string.h"

// NeoHex - Hex Editor with dual-pane view

namespace neohex {

static const int MAX_FILE_SIZE = INODE_SIZE * 1024; // INODE_SIZEKB max
static const int MAX_UNDO = 64;
static const int BYTES_PER_ROW = 16;

struct UndoEntry {
    int offset;
    unsigned char old_byte;
    unsigned char new_byte;
};

struct EditorState {
    unsigned char* buffer;
    unsigned char* modified_flags; // Bitfield: 1 = modified
    int file_size;
    int cursor_offset;
    int view_offset;
    int visible_rows;
    bool in_ascii_pane; // false = hex pane, true = ascii pane
    int hex_nibble; // 0 = high nibble, 1 = low nibble
    bool dirty;
    char filename[INODE_SIZE];
    char backup_name[260];
    UndoEntry undo[MAX_UNDO];
    int undo_count;
    int undo_pos;
    bool insert_mode;
    int selection_start;
    int selection_end;
    bool selecting;
};

static EditorState* ed = nullptr;

// --- Utility ---

static bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static bool is_printable(unsigned char c) { return c >= 0x20 && c <= 0x7E; }
static char to_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static bool is_modified(int offset) {
    if (!ed->modified_flags || offset < 0 || offset >= ed->file_size) return false;
    return (ed->modified_flags[offset / 8] >> (offset % 8)) & 1;
}

static void set_modified(int offset) {
    if (!ed->modified_flags || offset < 0) return;
    ed->modified_flags[offset / 8] |= (1 << (offset % 8));
}

static void add_undo(int offset, unsigned char old_val, unsigned char new_val) {
    if (ed->undo_count < MAX_UNDO) {
        UndoEntry& u = ed->undo[ed->undo_count++];
        u.offset = offset;
        u.old_byte = old_val;
        u.new_byte = new_val;
        ed->undo_pos = ed->undo_count;
    }
}

static void do_undo() {
    if (ed->undo_count <= 0) {
        neo::display::printf("Nothing to undo.\n");
        return;
    }
    ed->undo_count--;
    UndoEntry& u = ed->undo[ed->undo_count];
    ed->buffer[u.offset] = u.old_byte;
    ed->cursor_offset = u.offset;
}

static void set_byte(int offset, unsigned char val) {
    if (offset < 0 || offset >= ed->file_size) return;
    unsigned char old = ed->buffer[offset];
    if (old != val) {
        add_undo(offset, old, val);
        ed->buffer[offset] = val;
        set_modified(offset);
        ed->dirty = true;
    }
}

static void insert_byte(int offset, unsigned char val) {
    if (ed->file_size >= MAX_FILE_SIZE) return;
    if (offset < 0) offset = 0;
    if (offset > ed->file_size) offset = ed->file_size;

    // Shift bytes right
    for (int i = ed->file_size; i > offset; i--) {
        ed->buffer[i] = ed->buffer[i - 1];
    }
    ed->buffer[offset] = val;
    ed->file_size++;
    ed->dirty = true;
}

static void delete_byte(int offset) {
    if (ed->file_size <= 0 || offset < 0 || offset >= ed->file_size) return;

    // Shift bytes left
    for (int i = offset; i < ed->file_size - 1; i++) {
        ed->buffer[i] = ed->buffer[i + 1];
    }
    ed->file_size--;
    if (ed->cursor_offset >= ed->file_size && ed->file_size > 0) {
        ed->cursor_offset = ed->file_size - 1;
    }
    ed->dirty = true;
}

// --- Drawing ---

static void draw_header() {
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0, 6);
    neo::display::printf(" NeoHex");
    if (ed->filename[0]) {
        neo::display::printf(" - %s", ed->filename);
        if (ed->dirty) neo::display::printf(" [Modified]");
    }
    neo::display::printf(" | Size: %d bytes", ed->file_size);
    if (ed->insert_mode) neo::display::printf(" | INS");
    int cur_x = 40;
    for (int i = cur_x; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

static void draw_status() {
    int h = neo::display::get_height();
    neo::display::set_cursor(0, h - 2);
    neo::display::set_color(0, 6);
    int w = neo::display::get_width();

    neo::display::printf(" Offset: $%08X (%d)", ed->cursor_offset, ed->cursor_offset);
    if (ed->cursor_offset < ed->file_size) {
        unsigned char b = ed->buffer[ed->cursor_offset];
        neo::display::printf(" | Byte: $%02X (%d) '%c'", b, b, is_printable(b) ? b : '.');
    }
    neo::display::printf(" | Pane: %s", ed->in_ascii_pane ? "ASCII" : "HEX");
    int cur_x = 70;
    for (int i = cur_x; i < w; i++) neo::display::putchar(' ');

    neo::display::set_cursor(0, h - 1);
    neo::display::printf(" F1:Help F2:Save F3:Goto F4:Search F5:Insert F6:Delete F7:Undo F8:Pane F10:Quit");
    for (int i = 80; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

static void draw_hex_view() {
    int start_row = 2;
    ed->visible_rows = neo::display::get_height() - 4;

    // Ensure cursor visible
    int cursor_row = (ed->cursor_offset - ed->view_offset) / BYTES_PER_ROW;
    if (cursor_row < 0) {
        ed->view_offset = (ed->cursor_offset / BYTES_PER_ROW) * BYTES_PER_ROW;
    } else if (cursor_row >= ed->visible_rows) {
        ed->view_offset = ((ed->cursor_offset / BYTES_PER_ROW) - ed->visible_rows + 1) * BYTES_PER_ROW;
    }
    if (ed->view_offset < 0) ed->view_offset = 0;

    for (int row = 0; row < ed->visible_rows; row++) {
        int row_offset = ed->view_offset + row * BYTES_PER_ROW;
        neo::display::set_cursor(0, start_row + row);

        if (row_offset >= ed->file_size && row_offset > 0) {
            neo::display::clear_eol();
            continue;
        }

        // Address
        neo::display::set_fg(6); // Yellow
        neo::display::printf("%08X  ", row_offset);

        // Hex bytes
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            int off = row_offset + col;
            if (off < ed->file_size) {
                bool is_cursor = (off == ed->cursor_offset) && !ed->in_ascii_pane;
                bool is_mod = is_modified(off);

                if (is_cursor) {
                    neo::display::set_color(0, 7); // Inverted
                } else if (is_mod) {
                    neo::display::set_fg(1); // Red for modified
                } else {
                    neo::display::set_fg(7); // White
                }

                neo::display::printf("%02X", ed->buffer[off]);
                neo::display::set_color(7, 0);
                neo::display::putchar(' ');
            } else {
                neo::display::printf("   ");
            }

            if (col == 7) neo::display::putchar(' ');
        }

        neo::display::printf(" |");

        // ASCII pane
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            int off = row_offset + col;
            if (off < ed->file_size) {
                bool is_cursor = (off == ed->cursor_offset) && ed->in_ascii_pane;
                bool is_mod = is_modified(off);
                unsigned char b = ed->buffer[off];

                if (is_cursor) {
                    neo::display::set_color(0, 7);
                } else if (is_mod) {
                    neo::display::set_fg(1);
                } else {
                    neo::display::set_fg(2); // Green
                }

                neo::display::putchar(is_printable(b) ? b : '.');
                neo::display::set_color(7, 0);
            } else {
                neo::display::putchar(' ');
            }
        }

        neo::display::printf("|");
        neo::display::clear_eol();
    }
}

static void redraw() {
    draw_header();
    draw_hex_view();
    draw_status();
}

// --- File Operations ---

static bool load_file(const char* path) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) {
        neo::display::printf("Cannot open: %s\n", path);
        return false;
    }

    ed->file_size = neo::filesystem::read(fh, ed->buffer, MAX_FILE_SIZE);
    neo::filesystem::close(fh);

    neo_strncpy(ed->filename, path, 255);
    ed->filename[255] = 0;
    ed->dirty = false;
    ed->cursor_offset = 0;
    ed->view_offset = 0;
    ed->undo_count = 0;

    // Clear modified flags
    int flag_bytes = (MAX_FILE_SIZE + 7) / 8;
    neo_memset(ed->modified_flags, 0, flag_bytes);

    return true;
}

static bool save_file() {
    if (!ed->filename[0]) {
        neo::display::printf("No filename set.\n");
        return false;
    }

    // Create backup
    ksprintf(ed->backup_name, 260, "%s.bak", ed->filename);
    neo::filesystem::FileHandle bfh;
    if (neo::filesystem::open(bfh, ed->filename, neo::filesystem::MODE_READ) == 0) {
        unsigned char* tmpbuf = (unsigned char*)neo::mem::alloc(ed->file_size);
        if (tmpbuf) {
            int sz = neo::filesystem::read(bfh, tmpbuf, ed->file_size);
            neo::filesystem::close(bfh);
            neo::filesystem::FileHandle bkfh;
            if (neo::filesystem::open(bkfh, ed->backup_name, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
                neo::filesystem::write(bkfh, tmpbuf, sz);
                neo::filesystem::close(bkfh);
            }
            neo::mem::free(tmpbuf);
        } else {
            neo::filesystem::close(bfh);
        }
    }

    // Write
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, ed->filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) {
        neo::display::printf("Cannot write: %s\n", ed->filename);
        return false;
    }

    neo::filesystem::write(fh, ed->buffer, ed->file_size);
    neo::filesystem::close(fh);
    ed->dirty = false;

    // Clear modified flags
    int flag_bytes = (MAX_FILE_SIZE + 7) / 8;
    neo_memset(ed->modified_flags, 0, flag_bytes);

    return true;
}

// --- Search ---

static int search_hex_pattern(const unsigned char* pattern, int pat_len, int start) {
    for (int i = start; i <= ed->file_size - pat_len; i++) {
        bool match = true;
        for (int j = 0; j < pat_len; j++) {
            if (ed->buffer[i + j] != pattern[j]) { match = false; break; }
        }
        if (match) return i;
    }
    return -1;
}

static int search_text(const char* text, int start) {
    int tlen = neo_strlen(text);
    for (int i = start; i <= ed->file_size - tlen; i++) {
        bool match = true;
        for (int j = 0; j < tlen; j++) {
            if (ed->buffer[i + j] != (unsigned char)text[j]) { match = false; break; }
        }
        if (match) return i;
    }
    return -1;
}

// --- Command handling ---

static void show_help_dialog() {
    neo::display::clear();
    neo::display::set_fg(3);
    neo::display::printf("+===================================================+\n");
    neo::display::printf("|              NeoHex - Hex Editor Help              |\n");
    neo::display::printf("+===================================================+\n\n");
    neo::display::set_fg(7);

    neo::display::printf("Navigation:\n");
    neo::display::printf("  Arrow keys  - Move cursor\n");
    neo::display::printf("  Page Up/Dn  - Scroll page\n");
    neo::display::printf("  Home/End    - Start/end of file\n\n");

    neo::display::printf("Editing (Hex pane):\n");
    neo::display::printf("  0-9, A-F    - Enter hex digits\n\n");

    neo::display::printf("Editing (ASCII pane):\n");
    neo::display::printf("  Any key     - Enter ASCII character\n\n");

    neo::display::printf("Function Keys:\n");
    neo::display::printf("  F1          - This help\n");
    neo::display::printf("  F2          - Save (with .bak backup)\n");
    neo::display::printf("  F3          - Goto offset\n");
    neo::display::printf("  F4          - Search (hex or text)\n");
    neo::display::printf("  F5          - Toggle insert mode\n");
    neo::display::printf("  F6          - Delete byte at cursor\n");
    neo::display::printf("  F7          - Undo last change\n");
    neo::display::printf("  F8          - Switch hex/ascii pane\n");
    neo::display::printf("  F10 / Esc   - Quit\n\n");

    neo::display::printf("Commands (press ':'):\n");
    neo::display::printf("  :g <offset>          - Goto offset\n");
    neo::display::printf("  :s <hex bytes>       - Search hex pattern\n");
    neo::display::printf("  :t <text>            - Search text string\n");
    neo::display::printf("  :n                   - Search next\n");
    neo::display::printf("  :w [filename]        - Save\n");
    neo::display::printf("  :q                   - Quit\n");
    neo::display::printf("  :i                   - File info\n\n");

    neo::display::printf("Press any key to continue...\n");
    while (!neo::keyboard::key_available()) neo::timer::delay_ms(10);
    neo::keyboard::read_scancode();
}

static void command_mode() {
    char input[INODE_SIZE];
    int h = neo::display::get_height();
    neo::display::set_cursor(0, h - 1);
    neo::display::set_color(7, 0);
    neo::display::clear_eol();
    neo::console::getline(input, INODE_SIZE, ":");

    if (input[0] == 0) return;

    char cmd = to_upper(input[0]);
    const char* arg = input + 1;
    while (*arg == ' ') arg++;

    if (cmd == 'Q') {
        if (ed->dirty) {
            neo::display::printf("File modified! Save first (F2) or :q! to force quit.");
            if (neo_strlen(input) > 1 && input[1] == '!') {
                ed->file_size = -1; // Signal quit
            }
        } else {
            ed->file_size = -1; // Signal quit
        }
    } else if (cmd == 'W') {
        if (*arg) {
            neo_strncpy(ed->filename, arg, 255);
            ed->filename[255] = 0;
        }
        if (save_file()) {
            neo::display::printf("Saved %d bytes to %s", ed->file_size, ed->filename);
        }
    } else if (cmd == 'G') {
        // Goto offset
        unsigned int off = 0;
        if (*arg == '$' || (*arg == '0' && to_upper(*(arg+1)) == 'X')) {
            const char* p = (*arg == '$') ? arg + 1 : arg + 2;
            while (is_hex(*p)) {
                off = (off << 4) | hex_val(*p);
                p++;
            }
        } else {
            while (*arg >= '0' && *arg <= '9') {
                off = off * 10 + (*arg - '0');
                arg++;
            }
        }
        if ((int)off < ed->file_size) {
            ed->cursor_offset = off;
        }
    } else if (cmd == 'S') {
        // Search hex
        unsigned char pattern[64];
        int plen = 0;
        while (*arg && plen < 64) {
            while (*arg == ' ') arg++;
            if (!is_hex(*arg)) break;
            int hi = hex_val(*arg); arg++;
            int lo = 0;
            if (is_hex(*arg)) { lo = hex_val(*arg); arg++; }
            pattern[plen++] = (hi << 4) | lo;
        }
        int found = search_hex_pattern(pattern, plen, ed->cursor_offset + 1);
        if (found >= 0) {
            ed->cursor_offset = found;
            neo::display::printf("Found at offset $%08X", found);
        } else {
            neo::display::printf("Pattern not found.");
        }
    } else if (cmd == 'T') {
        // Search text
        int found = search_text(arg, ed->cursor_offset + 1);
        if (found >= 0) {
            ed->cursor_offset = found;
            neo::display::printf("Found at offset $%08X", found);
        } else {
            neo::display::printf("Text not found.");
        }
    } else if (cmd == 'I') {
        neo::display::printf("File: %s | Size: %d ($%X) | Undo: %d",
            ed->filename[0] ? ed->filename : "(none)",
            ed->file_size, ed->file_size, ed->undo_count);
    }
}

static void handle_hex_input(unsigned char scancode, bool shift) {
    char c = neo::keyboard::translate(scancode, shift);

    // Function keys (scancodes)
    if (scancode == 0x3B) { show_help_dialog(); return; } // F1
    if (scancode == 0x3C) { // F2 - Save
        if (save_file()) {
            // status will show saved
        }
        return;
    }
    if (scancode == 0x3D) { // F3 - Goto
        char input[32];
        int h = neo::display::get_height();
        neo::display::set_cursor(0, h - 1);
        neo::display::set_color(7, 0);
        neo::display::clear_eol();
        neo::console::getline(input, 32, "Goto offset: ");
        unsigned int off = 0;
        const char* p = input;
        if (*p == '$') p++;
        while (is_hex(*p)) { off = (off << 4) | hex_val(*p); p++; }
        if ((int)off < ed->file_size) ed->cursor_offset = off;
        return;
    }
    if (scancode == 0x3E) { // F4 - Search
        char input[128];
        int h = neo::display::get_height();
        neo::display::set_cursor(0, h - 1);
        neo::display::set_color(7, 0);
        neo::display::clear_eol();
        neo::console::getline(input, 128, "Search (hex or 'text'): ");
        if (input[0] == '\'') {
            int found = search_text(input + 1, ed->cursor_offset + 1);
            if (found >= 0) ed->cursor_offset = found;
        } else {
            unsigned char pat[64]; int plen = 0;
            const char* pp = input;
            while (*pp && plen < 64) {
                while (*pp == ' ') pp++;
                if (!is_hex(*pp)) break;
                int hi = hex_val(*pp); pp++;
                int lo = 0;
                if (is_hex(*pp)) { lo = hex_val(*pp); pp++; }
                pat[plen++] = (hi << 4) | lo;
            }
            int found = search_hex_pattern(pat, plen, ed->cursor_offset + 1);
            if (found >= 0) ed->cursor_offset = found;
        }
        return;
    }
    if (scancode == 0x3F) { ed->insert_mode = !ed->insert_mode; return; } // F5
    if (scancode == 0x40) { delete_byte(ed->cursor_offset); return; } // F6
    if (scancode == 0x41) { do_undo(); return; } // F7
    if (scancode == 0x42) { ed->in_ascii_pane = !ed->in_ascii_pane; ed->hex_nibble = 0; return; } // F8
    if (scancode == 0x44 || scancode == 0x01) { ed->file_size = -1; return; } // F10/Esc - quit

    // Colon for command mode
    if (c == ':') { command_mode(); return; }

    // Navigation
    if (scancode == 0x4D) { // Right
        if (ed->in_ascii_pane) {
            if (ed->cursor_offset < ed->file_size - 1) ed->cursor_offset++;
        } else {
            ed->hex_nibble++;
            if (ed->hex_nibble > 1) {
                ed->hex_nibble = 0;
                if (ed->cursor_offset < ed->file_size - 1) ed->cursor_offset++;
            }
        }
        return;
    }
    if (scancode == 0x4B) { // Left
        if (ed->in_ascii_pane) {
            if (ed->cursor_offset > 0) ed->cursor_offset--;
        } else {
            ed->hex_nibble--;
            if (ed->hex_nibble < 0) {
                ed->hex_nibble = 1;
                if (ed->cursor_offset > 0) ed->cursor_offset--;
            }
        }
        return;
    }
    if (scancode == 0x48) { // Up
        if (ed->cursor_offset >= BYTES_PER_ROW) ed->cursor_offset -= BYTES_PER_ROW;
        return;
    }
    if (scancode == 0x50) { // Down
        if (ed->cursor_offset + BYTES_PER_ROW < ed->file_size) ed->cursor_offset += BYTES_PER_ROW;
        return;
    }
    if (scancode == 0x49) { // Page Up
        int page = ed->visible_rows * BYTES_PER_ROW;
        ed->cursor_offset -= page;
        if (ed->cursor_offset < 0) ed->cursor_offset = 0;
        return;
    }
    if (scancode == 0x51) { // Page Down
        int page = ed->visible_rows * BYTES_PER_ROW;
        ed->cursor_offset += page;
        if (ed->cursor_offset >= ed->file_size) ed->cursor_offset = ed->file_size - 1;
        if (ed->cursor_offset < 0) ed->cursor_offset = 0;
        return;
    }
    if (scancode == 0x47) { ed->cursor_offset = 0; return; } // Home
    if (scancode == 0x4F) { ed->cursor_offset = ed->file_size - 1; return; } // End

    // Editing
    if (ed->in_ascii_pane) {
        if (c >= 0x20 && c <= 0x7E) {
            if (ed->insert_mode) {
                insert_byte(ed->cursor_offset, c);
            } else {
                set_byte(ed->cursor_offset, c);
            }
            if (ed->cursor_offset < ed->file_size - 1) ed->cursor_offset++;
        }
    } else {
        // Hex pane
        char uc = to_upper(c);
        if (is_hex(uc)) {
            int nib = hex_val(uc);
            unsigned char cur = (ed->cursor_offset < ed->file_size) ? ed->buffer[ed->cursor_offset] : 0;
            unsigned char new_val;

            if (ed->hex_nibble == 0) {
                new_val = (nib << 4) | (cur & 0x0F);
                set_byte(ed->cursor_offset, new_val);
                ed->hex_nibble = 1;
            } else {
                new_val = (cur & 0xF0) | nib;
                set_byte(ed->cursor_offset, new_val);
                ed->hex_nibble = 0;
                if (ed->cursor_offset < ed->file_size - 1) ed->cursor_offset++;
            }
        }
    }
}

static void new_file() {
    ed->file_size = 0;
    ed->cursor_offset = 0;
    ed->view_offset = 0;
    ed->dirty = false;
    ed->filename[0] = 0;
    ed->undo_count = 0;
    // Put at least one byte
    ed->buffer[0] = 0;
    ed->file_size = 1;
}

} // namespace neohex

extern "C" void app_main(int argc, char** argv) {
    neohex::ed = (neohex::EditorState*)neo::mem::alloc(sizeof(neohex::EditorState));
    neo_memset(neohex::ed, 0, sizeof(neohex::EditorState));

    neohex::ed->buffer = (unsigned char*)neo::mem::alloc(neohex::MAX_FILE_SIZE);
    neohex::ed->modified_flags = (unsigned char*)neo::mem::alloc((neohex::MAX_FILE_SIZE + 7) / 8);
    neo_memset(neohex::ed->modified_flags, 0, (neohex::MAX_FILE_SIZE + 7) / 8);

    if (argc >= 2) {
        if (!neohex::load_file(argv[1])) {
            neohex::new_file();
            neo_strncpy(neohex::ed->filename, argv[1], 255);
        }
    } else {
        neohex::new_file();
    }

    neo::display::clear();

    // Main edit loop
    while (neohex::ed->file_size >= 0) {
        neohex::redraw();

        // Wait for keypress
        while (!neo::keyboard::key_available()) {
            neo::timer::delay_ms(20);
        }

        unsigned char scancode = neo::keyboard::read_scancode();
        bool shift = neo::keyboard::is_shift_down();
        neohex::handle_hex_input(scancode, shift);
    }

    neo::display::clear();
    neo::display::printf("NeoHex: Goodbye.\n");

    neo::mem::free(neohex::ed->modified_flags);
    neo::mem::free(neohex::ed->buffer);
    neo::mem::free(neohex::ed);
}
