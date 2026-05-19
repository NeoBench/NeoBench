#include "../include/neobench.h"
#include "../lib/string.h"

// NeoComm - Serial Terminal & BBS Client for NeoBench
// VT100 emulation, phonebook, capture, XMODEM, macros

namespace neocomm {

// --- Serial Config ---
struct SerialConfig {
    int baud_rate;
    int data_bits;
    int parity;   // 0=none, 1=even, 2=odd
    int stop_bits;
    bool flow_control;
};

static SerialConfig serial_cfg;
static bool connected = false;
static bool online = false;

// --- VT100 Terminal State ---
struct TermState {
    int cursor_x, cursor_y;
    int scroll_top, scroll_bottom;
    int screen_width, screen_height;
    int fg_color, bg_color;
    bool bold;
    bool inverse;
    bool wrap_mode;
    bool insert_mode;
    // Saved cursor
    int saved_x, saved_y;
    // ESC sequence parser
    bool in_escape;
    bool in_csi;
    char esc_buf[32];
    int esc_len;
};

static TermState term;

// --- Capture ---
static bool capture_active = false;
static neo::filesystem::FileHandle capture_fh;
static char capture_filename[INODE_SIZE];

// --- Phonebook ---
struct PhonebookEntry {
    char name[64];
    char number[32]; // phone or address
    int baud;
    int data_bits;
    int parity;
    int stop_bits;
    bool valid;
};

static const int MAX_PHONEBOOK = 16;
static PhonebookEntry phonebook[MAX_PHONEBOOK];
static int phonebook_count = 0;

// --- Macro Keys ---
struct MacroKey {
    int scancode;
    char text[128];
    bool valid;
};

static const int MAX_MACROS = 10;
static MacroKey macros[MAX_MACROS];
static int macro_count = 0;

// --- Auto-answer ---
static bool auto_answer = false;

// --- VT100 Terminal Emulation ---
void term_init() {
    term.cursor_x = 0;
    term.cursor_y = 0;
    term.screen_width = neo::display::get_width();
    term.screen_height = neo::display::get_height() - 2; // Reserve status line
    term.scroll_top = 0;
    term.scroll_bottom = term.screen_height - 1;
    term.fg_color = 7;
    term.bg_color = 0;
    term.bold = false;
    term.inverse = false;
    term.wrap_mode = true;
    term.insert_mode = false;
    term.saved_x = 0;
    term.saved_y = 0;
    term.in_escape = false;
    term.in_csi = false;
    term.esc_len = 0;
}

void term_scroll_up() {
    // Simple scroll: just move cursor
    // In real terminal, would scroll buffer
    term.cursor_y = term.scroll_bottom;
}

void term_newline() {
    term.cursor_x = 0;
    term.cursor_y++;
    if (term.cursor_y > term.scroll_bottom) {
        term_scroll_up();
    }
    neo::display::set_cursor(term.cursor_x, term.cursor_y);
}

void term_putchar(char c) {
    if (capture_active) {
        neo::filesystem::write(capture_fh, &c, 1);
    }

    neo::display::set_cursor(term.cursor_x, term.cursor_y);
    neo::display::putchar(c);
    term.cursor_x++;

    if (term.cursor_x >= term.screen_width) {
        if (term.wrap_mode) {
            term_newline();
        } else {
            term.cursor_x = term.screen_width - 1;
        }
    }
}

// Parse CSI parameters
int parse_csi_params(const char* buf, int len, int* params, int max_params) {
    int count = 0;
    int val = 0;
    bool has_val = false;

    for (int i = 0; i < len && count < max_params; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            val = val * 10 + (buf[i] - '0');
            has_val = true;
        } else if (buf[i] == ';') {
            params[count++] = has_val ? val : 0;
            val = 0;
            has_val = false;
        }
    }
    if (has_val && count < max_params) params[count++] = val;
    return count;
}

void term_process_csi(char final_char) {
    int params[8] = {0};
    int nparam = parse_csi_params(term.esc_buf, term.esc_len, params, 8);

    switch (final_char) {
        case 'A': // Cursor Up
            term.cursor_y -= (nparam > 0 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_y < term.scroll_top) term.cursor_y = term.scroll_top;
            break;
        case 'B': // Cursor Down
            term.cursor_y += (nparam > 0 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_y > term.scroll_bottom) term.cursor_y = term.scroll_bottom;
            break;
        case 'C': // Cursor Forward
            term.cursor_x += (nparam > 0 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_x >= term.screen_width) term.cursor_x = term.screen_width - 1;
            break;
        case 'D': // Cursor Back
            term.cursor_x -= (nparam > 0 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_x < 0) term.cursor_x = 0;
            break;
        case 'H': // Cursor Position
        case 'f':
            term.cursor_y = (nparam > 0 && params[0] > 0) ? params[0] - 1 : 0;
            term.cursor_x = (nparam > 1 && params[1] > 0) ? params[1] - 1 : 0;
            if (term.cursor_y > term.scroll_bottom) term.cursor_y = term.scroll_bottom;
            if (term.cursor_x >= term.screen_width) term.cursor_x = term.screen_width - 1;
            break;
        case 'J': // Erase Display
            if (params[0] == 0) {
                // Clear from cursor to end
                neo::display::clear_eol();
            } else if (params[0] == 2) {
                neo::display::clear();
                term.cursor_x = 0;
                term.cursor_y = 0;
            }
            break;
        case 'K': // Erase Line
            neo::display::clear_eol();
            break;
        case 'm': // SGR - Set Graphic Rendition
            for (int i = 0; i < nparam; i++) {
                switch (params[i]) {
                    case 0: // Reset
                        term.fg_color = 7; term.bg_color = 0;
                        term.bold = false; term.inverse = false;
                        break;
                    case 1: term.bold = true; break;
                    case 7: term.inverse = true; break;
                    case 27: term.inverse = false; break;
                    case 30: term.fg_color = 0; break;
                    case 31: term.fg_color = 4; break;  // Red
                    case 32: term.fg_color = 2; break;  // Green
                    case 33: term.fg_color = 6; break;  // Yellow
                    case 34: term.fg_color = 1; break;  // Blue
                    case 35: term.fg_color = 5; break;  // Magenta
                    case 36: term.fg_color = 3; break;  // Cyan
                    case 37: term.fg_color = 7; break;  // White
                    case 40: term.bg_color = 0; break;
                    case 41: term.bg_color = 4; break;
                    case 42: term.bg_color = 2; break;
                    case 43: term.bg_color = 6; break;
                    case 44: term.bg_color = 1; break;
                    case 45: term.bg_color = 5; break;
                    case 46: term.bg_color = 3; break;
                    case 47: term.bg_color = 7; break;
                }
            }
            if (nparam == 0) {
                term.fg_color = 7; term.bg_color = 0;
                term.bold = false; term.inverse = false;
            }
            if (term.inverse)
                neo::display::set_color(term.bg_color, term.fg_color);
            else
                neo::display::set_color(term.bold ? term.fg_color + 8 : term.fg_color, term.bg_color);
            neo::display::set_bold(term.bold);
            break;
        case 'r': // Set Scrolling Region
            term.scroll_top = (nparam > 0 && params[0] > 0) ? params[0] - 1 : 0;
            term.scroll_bottom = (nparam > 1 && params[1] > 0) ? params[1] - 1 : term.screen_height - 1;
            break;
        case 's': // Save cursor
            term.saved_x = term.cursor_x;
            term.saved_y = term.cursor_y;
            break;
        case 'u': // Restore cursor
            term.cursor_x = term.saved_x;
            term.cursor_y = term.saved_y;
            break;
        case 'h': // Set mode
            if (params[0] == 7) term.wrap_mode = true;
            break;
        case 'l': // Reset mode
            if (params[0] == 7) term.wrap_mode = false;
            break;
    }
    neo::display::set_cursor(term.cursor_x, term.cursor_y);
}

void term_process_char(char c) {
    if (term.in_escape) {
        if (term.in_csi) {
            if (c >= 0x40 && c <= 0x7E) {
                // Final character
                term_process_csi(c);
                term.in_escape = false;
                term.in_csi = false;
                term.esc_len = 0;
            } else if (term.esc_len < 30) {
                term.esc_buf[term.esc_len++] = c;
            }
        } else {
            switch (c) {
                case '[': // CSI
                    term.in_csi = true;
                    term.esc_len = 0;
                    break;
                case '7': // Save cursor
                    term.saved_x = term.cursor_x;
                    term.saved_y = term.cursor_y;
                    term.in_escape = false;
                    break;
                case '8': // Restore cursor
                    term.cursor_x = term.saved_x;
                    term.cursor_y = term.saved_y;
                    neo::display::set_cursor(term.cursor_x, term.cursor_y);
                    term.in_escape = false;
                    break;
                case 'D': // Index (scroll up)
                    term.cursor_y++;
                    if (term.cursor_y > term.scroll_bottom) term_scroll_up();
                    neo::display::set_cursor(term.cursor_x, term.cursor_y);
                    term.in_escape = false;
                    break;
                case 'M': // Reverse Index (scroll down)
                    term.cursor_y--;
                    if (term.cursor_y < term.scroll_top) term.cursor_y = term.scroll_top;
                    neo::display::set_cursor(term.cursor_x, term.cursor_y);
                    term.in_escape = false;
                    break;
                case 'c': // Reset
                    term_init();
                    neo::display::clear();
                    term.in_escape = false;
                    break;
                default:
                    term.in_escape = false;
                    break;
            }
        }
        return;
    }

    switch (c) {
        case 0x1B: // ESC
            term.in_escape = true;
            term.in_csi = false;
            term.esc_len = 0;
            break;
        case '\r': // CR
            term.cursor_x = 0;
            neo::display::set_cursor(term.cursor_x, term.cursor_y);
            break;
        case '\n': // LF
            term_newline();
            break;
        case '\b': // BS
            if (term.cursor_x > 0) term.cursor_x--;
            neo::display::set_cursor(term.cursor_x, term.cursor_y);
            break;
        case '\t': // Tab
            term.cursor_x = (term.cursor_x + 8) & ~7;
            if (term.cursor_x >= term.screen_width) term.cursor_x = term.screen_width - 1;
            neo::display::set_cursor(term.cursor_x, term.cursor_y);
            break;
        case 0x07: // Bell
            neo::audio::play_tone(0, 800, 100);
            break;
        default:
            if (c >= 0x20) term_putchar(c);
            break;
    }
}

// --- XMODEM Protocol ---
static const unsigned char XMODEM_SOH = 0x01;
static const unsigned char XMODEM_EOT = 0x04;
static const unsigned char XMODEM_ACK = 0x06;
static const unsigned char XMODEM_NAK = 0x15;
static const unsigned char XMODEM_CAN = 0x18;

struct XmodemState {
    unsigned char block_num;
    unsigned int bytes_transferred;
    unsigned int total_bytes;
    bool active;
};

unsigned char xmodem_checksum(const unsigned char* data, int len) {
    unsigned char sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return sum;
}

void xmodem_send_sim(const char* filename) {
    neo::display::printf("\nXMODEM Send: %s\n", filename);

    // Simulate sending
    unsigned int total = 8192; // simulated file size
    unsigned int sent = 0;
    unsigned char block = 1;
    int w = neo::display::get_width();

    while (sent < total) {
        neo::display::printf("\rBlock %3d: ", block);
        int bar_width = w - 20;
        int filled = (int)((unsigned long long)sent * bar_width / total);
        neo::display::putchar('[');
        for (int i = 0; i < bar_width; i++)
            neo::display::putchar(i < filled ? '#' : '.');
        neo::display::putchar(']');

        neo::timer::delay_ms(50);
        sent += 128;
        block++;

        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc == 0x01) { // ESC = cancel
                neo::display::puts("\nTransfer cancelled.\n");
                return;
            }
        }
    }
    neo::display::printf("\nTransfer complete: %d bytes in %d blocks\n", total, block - 1);
}

void xmodem_recv_sim(const char* filename) {
    neo::display::printf("\nXMODEM Receive: %s\n", filename);
    neo::display::puts("Waiting for sender (press ESC to cancel)...\n");
    neo::timer::delay_ms(500);

    unsigned int total = 6400;
    unsigned int recv = 0;
    unsigned char block = 1;
    int w = neo::display::get_width();

    while (recv < total) {
        neo::display::printf("\rBlock %3d: ", block);
        int bar_width = w - 20;
        int filled = (int)((unsigned long long)recv * bar_width / total);
        neo::display::putchar('[');
        for (int i = 0; i < bar_width; i++)
            neo::display::putchar(i < filled ? '#' : '.');
        neo::display::putchar(']');

        neo::timer::delay_ms(80);
        recv += 128;
        block++;

        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc == 0x01) {
                neo::display::puts("\nTransfer cancelled.\n");
                return;
            }
        }
    }
    neo::display::printf("\nReceived: %d bytes in %d blocks\n", total, block - 1);
}

// --- BBS Simulation ---
static unsigned int bbs_rng = 0;

void bbs_simulate_output() {
    bbs_rng = bbs_rng * 1103515245 + 12345;

    // Simulate various BBS screens
    static int bbs_state = 0;
    static int bbs_timer = 0;

    bbs_timer++;
    if (bbs_timer < 50) return;
    bbs_timer = 0;

    switch (bbs_state) {
        case 0: { // Banner
            const char* banner =
                "\x1b[2J\x1b[1;34m"
                "  ___________________________________\r\n"
                " |                                   |\r\n"
                " |   Welcome to Amiga BBS v3.0       |\r\n"
                " |   Running on NeoBench Kernel      |\r\n"
                " |   Node 1 of 4                     |\r\n"
                " |___________________________________|\r\n"
                "\x1b[0m\r\n"
                " Sysop: CopperMaster\r\n"
                " Users online: 3\r\n"
                " Last caller: BlitterKing\r\n\r\n"
                "\x1b[1;33mMain Menu:\x1b[0m\r\n"
                " [M] Message boards\r\n"
                " [F] File areas\r\n"
                " [C] Chat\r\n"
                " [U] User list\r\n"
                " [G] Goodbye (logoff)\r\n\r\n"
                "\x1b[1;32mCommand> \x1b[0m";
            for (int i = 0; banner[i]; i++) {
                term_process_char(banner[i]);
            }
            bbs_state = 1;
            break;
        }
        case 1:
            // Waiting for input - handled by main loop
            break;
    }
}

// --- Status Bar ---
void draw_status() {
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    neo::display::set_cursor(0, h - 1);
    neo::display::set_color(0, 3);

    const char* parity_str[] = {"N", "E", "O"};
    neo::display::printf(" %d-%d-%s-%d | %s | %s | %s ",
                         serial_cfg.baud_rate, serial_cfg.data_bits,
                         parity_str[serial_cfg.parity], serial_cfg.stop_bits,
                         online ? "ONLINE" : "OFFLINE",
                         capture_active ? "CAP" : "   ",
                         auto_answer ? "AA" : "  ");

    neo::display::puts("| F1:Help F2:Phone F3:Cap F4:Send F5:Recv F10:Menu ");
    int used = 60;
    for (int i = used; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

// --- Phonebook UI ---
void show_phonebook() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|         Phonebook              |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);

    neo::display::puts("\n  #  | Name                     | Number/Address    | Baud\n");
    neo::display::puts("  ---+---------------------------+-------------------+------\n");

    for (int i = 0; i < phonebook_count; i++) {
        if (!phonebook[i].valid) continue;
        neo::display::printf("  %2d | %-25s | %-17s | %d\n",
                             i + 1, phonebook[i].name, phonebook[i].number, phonebook[i].baud);
    }
    if (phonebook_count == 0) {
        neo::display::puts("  (Phonebook empty)\n");
    }

    neo::display::puts("\n[d]ial #  [a]dd  [e]dit  [r]emove  [b]ack: ");
    char input[64];
    neo::console::getline(input, sizeof(input), "");

    if (input[0] == 'a' || input[0] == 'A') {
        if (phonebook_count < MAX_PHONEBOOK) {
            PhonebookEntry& e = phonebook[phonebook_count];
            neo::console::getline(e.name, 64, "Name: ");
            neo::console::getline(e.number, 32, "Number/Address: ");
            char baudstr[16];
            neo::console::getline(baudstr, 16, "Baud [9600]: ");
            e.baud = 9600;
            if (baudstr[0]) {
                e.baud = 0;
                for (int i = 0; baudstr[i] >= '0' && baudstr[i] <= '9'; i++)
                    e.baud = e.baud * 10 + (baudstr[i] - '0');
            }
            e.data_bits = 8;
            e.parity = 0;
            e.stop_bits = 1;
            e.valid = true;
            phonebook_count++;
            neo::display::puts("Entry added.\n");
        }
    } else if (input[0] == 'd' || input[0] == 'D') {
        // Dial
        int num = 0;
        for (int i = 1; input[i] >= '0' && input[i] <= '9'; i++)
            num = num * 10 + (input[i] - '0');
        if (input[1] == ' ') {
            num = 0;
            for (int i = 2; input[i] >= '0' && input[i] <= '9'; i++)
                num = num * 10 + (input[i] - '0');
        }
        if (num >= 1 && num <= phonebook_count && phonebook[num - 1].valid) {
            PhonebookEntry& e = phonebook[num - 1];
            serial_cfg.baud_rate = e.baud;
            serial_cfg.data_bits = e.data_bits;
            serial_cfg.parity = e.parity;
            serial_cfg.stop_bits = e.stop_bits;

            neo::display::printf("\nDialing %s (%s)...\n", e.name, e.number);
            neo::timer::delay_ms(500);
            neo::display::puts("ATDT");
            neo::display::puts(e.number);
            neo::display::puts("\n");
            neo::timer::delay_ms(1000);
            neo::display::puts("CONNECT ");
            neo::display::printf("%d\n", e.baud);
            neo::timer::delay_ms(300);
            online = true;
        }
    }
}

// --- Settings Menu ---
void show_serial_settings() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|     Serial Port Settings       |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);

    const char* parity_names[] = {"None", "Even", "Odd"};
    neo::display::printf("\n  1. Baud Rate:    %d\n", serial_cfg.baud_rate);
    neo::display::printf("  2. Data Bits:    %d\n", serial_cfg.data_bits);
    neo::display::printf("  3. Parity:       %s\n", parity_names[serial_cfg.parity]);
    neo::display::printf("  4. Stop Bits:    %d\n", serial_cfg.stop_bits);
    neo::display::printf("  5. Flow Control: %s\n", serial_cfg.flow_control ? "RTS/CTS" : "None");

    neo::display::puts("\nSelect option (1-5) or [b]ack: ");
    char input[16];
    neo::console::getline(input, sizeof(input), "");

    if (input[0] == '1') {
        neo::display::puts("Baud rates: 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200\n");
        char baud[16];
        neo::console::getline(baud, 16, "New baud rate: ");
        int b = 0;
        for (int i = 0; baud[i] >= '0' && baud[i] <= '9'; i++) b = b * 10 + (baud[i] - '0');
        if (b > 0) {
            serial_cfg.baud_rate = b;
            neo::serial::init(b);
        }
    } else if (input[0] == '2') {
        serial_cfg.data_bits = (serial_cfg.data_bits == 8) ? 7 : 8;
    } else if (input[0] == '3') {
        serial_cfg.parity = (serial_cfg.parity + 1) % 3;
    } else if (input[0] == '4') {
        serial_cfg.stop_bits = (serial_cfg.stop_bits == 1) ? 2 : 1;
    } else if (input[0] == '5') {
        serial_cfg.flow_control = !serial_cfg.flow_control;
    }
}

// --- Macros ---
void show_macros() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|        Macro Keys              |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);

    neo::display::puts("\nDefined macros:\n\n");
    for (int i = 0; i < macro_count; i++) {
        if (!macros[i].valid) continue;
        neo::display::printf("  Shift+F%d: %s\n", i + 1, macros[i].text);
    }
    if (macro_count == 0) neo::display::puts("  (No macros defined)\n");

    neo::display::puts("\n[a]dd macro  [d]elete  [b]ack: ");
    char input[16];
    neo::console::getline(input, sizeof(input), "");

    if (input[0] == 'a' && macro_count < MAX_MACROS) {
        char key[8], text[128];
        neo::console::getline(key, 8, "Key (F1-F10): ");
        neo::console::getline(text, 128, "Text to send: ");
        if (text[0]) {
            macros[macro_count].scancode = 0x50 + (key[1] - '1'); // F-keys
            neo_strncpy(macros[macro_count].text, text, 127);
            macros[macro_count].valid = true;
            macro_count++;
        }
    }
}

// --- Help ---
void show_help() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|   NeoComm Help                 |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);

    neo::display::puts("\n  Key Bindings:\n\n");
    neo::display::puts("  F1        - This help screen\n");
    neo::display::puts("  F2        - Phonebook\n");
    neo::display::puts("  F3        - Toggle capture to file\n");
    neo::display::puts("  F4        - Send file (XMODEM)\n");
    neo::display::puts("  F5        - Receive file (XMODEM)\n");
    neo::display::puts("  F6        - Serial settings\n");
    neo::display::puts("  F7        - Macro keys\n");
    neo::display::puts("  F8        - Toggle auto-answer\n");
    neo::display::puts("  F9        - Clear screen\n");
    neo::display::puts("  F10       - Menu / Exit\n");
    neo::display::puts("  Ctrl+D    - Hang up / Disconnect\n\n");
    neo::display::puts("  VT100 terminal emulation active.\n");
    neo::display::puts("  Supports: cursor movement, colors, clear,\n");
    neo::display::puts("  scrolling regions, save/restore cursor.\n\n");

    neo::display::puts("  Press any key to continue...");
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

// --- Main menu ---
bool show_main_menu() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|   NeoComm v1.0                 |\n");
    neo::display::puts("|   Serial Terminal & BBS Client |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);

    neo::display::puts("\n  1. Terminal mode\n");
    neo::display::puts("  2. Phonebook\n");
    neo::display::puts("  3. Serial settings\n");
    neo::display::puts("  4. Macro keys\n");
    neo::display::puts("  5. Help\n");
    neo::display::puts("  Q. Quit\n\n");

    char input[16];
    neo::console::getline(input, sizeof(input), "Select: ");

    switch (input[0]) {
        case '1': return true; // Enter terminal mode
        case '2': show_phonebook(); return true;
        case '3': show_serial_settings(); return true;
        case '4': show_macros(); return true;
        case '5': show_help(); return true;
        case 'q': case 'Q': return false;
    }
    return true;
}

} // namespace neocomm

extern "C" void app_main(int argc, char** argv) {
    using namespace neocomm;

    // Initialize
    serial_cfg.baud_rate = 9600;
    serial_cfg.data_bits = 8;
    serial_cfg.parity = 0;
    serial_cfg.stop_bits = 1;
    serial_cfg.flow_control = false;
    connected = false;
    online = false;
    capture_active = false;
    auto_answer = false;
    phonebook_count = 0;
    macro_count = 0;
    bbs_rng = neo::timer::get_ticks();

    neo_memset(phonebook, 0, sizeof(phonebook));
    neo_memset(macros, 0, sizeof(macros));

    // Default phonebook entries
    neo_strcpy(phonebook[0].name, "Amiga BBS");
    neo_strcpy(phonebook[0].number, "555-0100");
    phonebook[0].baud = 9600;
    phonebook[0].data_bits = 8;
    phonebook[0].parity = 0;
    phonebook[0].stop_bits = 1;
    phonebook[0].valid = true;

    neo_strcpy(phonebook[1].name, "Retro Computing BBS");
    neo_strcpy(phonebook[1].number, "555-0200");
    phonebook[1].baud = 2400;
    phonebook[1].data_bits = 8;
    phonebook[1].parity = 0;
    phonebook[1].stop_bits = 1;
    phonebook[1].valid = true;

    neo_strcpy(phonebook[2].name, "DemoScene HQ");
    neo_strcpy(phonebook[2].number, "555-0300");
    phonebook[2].baud = 19200;
    phonebook[2].data_bits = 8;
    phonebook[2].parity = 0;
    phonebook[2].stop_bits = 1;
    phonebook[2].valid = true;

    phonebook_count = 3;

    // Default macros
    neo_strcpy(macros[0].text, "ATZ\r");
    macros[0].scancode = 0x50;
    macros[0].valid = true;
    neo_strcpy(macros[1].text, "ATDT");
    macros[1].scancode = 0x51;
    macros[1].valid = true;
    macro_count = 2;

    // Initialize serial
    neo::serial::init(serial_cfg.baud_rate);
    neo::audio::init();

    term_init();

    // Main menu loop
    bool running = true;
    bool in_terminal = false;

    while (running) {
        if (!in_terminal) {
            running = show_main_menu();
            if (!running) break;
            in_terminal = true;

            // Enter terminal mode
            neo::display::clear();
            term_init();

            // Show initial message
            const char* welcome =
                "\x1b[1;33mNeoComm Terminal Ready\x1b[0m\r\n"
                "Type AT commands or use F2 for phonebook.\r\n\r\n";
            for (int i = 0; welcome[i]; i++) term_process_char(welcome[i]);

            draw_status();
        }

        // Simulate BBS activity if online
        if (online) {
            bbs_simulate_output();
        }

        if (!neo::keyboard::key_available()) {
            neo::proc::yield();
            continue;
        }

        unsigned char sc = neo::keyboard::read_scancode();
        bool shift = neo::keyboard::is_shift_down();

        // Function keys
        if (sc == 0x50) { show_help(); term_init(); neo::display::clear(); draw_status(); continue; }
        if (sc == 0x51) { show_phonebook(); neo::display::clear(); term_init(); draw_status(); continue; }
        if (sc == 0x52) { // F3 - Capture toggle
            if (capture_active) {
                neo::filesystem::close(capture_fh);
                capture_active = false;
                const char* msg = "\r\n*** Capture OFF ***\r\n";
                for (int i = 0; msg[i]; i++) term_process_char(msg[i]);
            } else {
                neo_strcpy(capture_filename, "DF0:capture.txt");
                if (neo::filesystem::open(capture_fh, capture_filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
                    capture_active = true;
                    const char* msg = "\r\n*** Capture ON ***\r\n";
                    for (int i = 0; msg[i]; i++) term_process_char(msg[i]);
                }
            }
            draw_status();
            continue;
        }
        if (sc == 0x53) { // F4 - Send file
            char fname[128];
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 2);
            neo::display::clear_eol();
            neo::console::getline(fname, sizeof(fname), "Send file: ");
            if (fname[0]) xmodem_send_sim(fname);
            draw_status();
            continue;
        }
        if (sc == 0x54) { // F5 - Receive file
            char fname[128];
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 2);
            neo::display::clear_eol();
            neo::console::getline(fname, sizeof(fname), "Save as: ");
            if (fname[0]) xmodem_recv_sim(fname);
            draw_status();
            continue;
        }
        if (sc == 0x55) { show_serial_settings(); neo::display::clear(); term_init(); draw_status(); continue; }
        if (sc == 0x56) { show_macros(); neo::display::clear(); term_init(); draw_status(); continue; }
        if (sc == 0x57) { // F8 - Auto-answer
            auto_answer = !auto_answer;
            draw_status();
            continue;
        }
        if (sc == 0x58) { // F9 - Clear
            neo::display::clear();
            term_init();
            draw_status();
            continue;
        }
        if (sc == 0x59) { // F10 - Menu
            in_terminal = false;
            continue;
        }

        // Check shift+F keys for macros
        if (shift && sc >= 0x50 && sc <= 0x59) {
            int mi = sc - 0x50;
            if (mi < macro_count && macros[mi].valid) {
                for (int i = 0; macros[mi].text[i]; i++) {
                    neo::serial::putchar(macros[mi].text[i]);
                    term_process_char(macros[mi].text[i]);
                }
                draw_status();
                continue;
            }
        }

        // Regular character
        char ch = neo::keyboard::translate(sc, shift);
        if (ch) {
            // Send to serial port
            neo::serial::putchar(ch);

            // Local echo
            if (ch == '\n') {
                term_process_char('\r');
                term_process_char('\n');
            } else {
                term_process_char(ch);
            }
            draw_status();
        }
    }

    // Cleanup
    if (capture_active) {
        neo::filesystem::close(capture_fh);
    }
    if (online) {
        neo::serial::puts("+++\r\n");
        neo::timer::delay_ms(500);
        neo::serial::puts("ATH0\r\n");
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    neo::display::puts("NeoComm session ended.\n");
}
