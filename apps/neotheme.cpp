#include "../include/neobench.h"
#include "../lib/string.h"

// NeoTheme - Theme Engine
// Manage color schemes, preview, save/load themes

namespace neotheme {

static const int MAX_THEMES = 16;
static const int MAX_ELEMENTS = 12;

// UI element types
enum Element {
    ELEM_TITLE_FG = 0,
    ELEM_TITLE_BG,
    ELEM_MENU_FG,
    ELEM_MENU_BG,
    ELEM_TEXT_FG,
    ELEM_TEXT_BG,
    ELEM_HIGHLIGHT_FG,
    ELEM_HIGHLIGHT_BG,
    ELEM_STATUS_FG,
    ELEM_STATUS_BG,
    ELEM_BORDER_FG,
    ELEM_BORDER_BG,
};

static const char* element_names[] = {
    "Title FG",    "Title BG",
    "Menu FG",     "Menu BG",
    "Text FG",     "Text BG",
    "Hilight FG",  "Hilight BG",
    "Status FG",   "Status BG",
    "Border FG",   "Border BG",
};

static const char* color_names[] = {
    "Black", "DkBlue", "DkGreen", "DkCyan",
    "DkRed", "DkMagenta", "Brown", "LtGray",
    "DkGray", "Blue", "Green", "Cyan",
    "Red", "Magenta", "Yellow", "White"
};

struct Theme {
    char name[32];
    unsigned char colors[MAX_ELEMENTS];
};

static Theme themes[MAX_THEMES];
static int theme_count = 0;
static int current_theme = 0;
static int selected_element = 0;

// --- Built-in themes ---
static void init_themes() {
    theme_count = 0;

    // Default
    Theme& t0 = themes[theme_count++];
    neo_strcpy(t0.name, "Default");
    t0.colors[ELEM_TITLE_FG]     = 14;  // Yellow
    t0.colors[ELEM_TITLE_BG]     = 1;   // DkBlue
    t0.colors[ELEM_MENU_FG]      = 15;  // White
    t0.colors[ELEM_MENU_BG]      = 0;   // Black
    t0.colors[ELEM_TEXT_FG]       = 7;   // LtGray
    t0.colors[ELEM_TEXT_BG]       = 0;   // Black
    t0.colors[ELEM_HIGHLIGHT_FG]  = 0;   // Black
    t0.colors[ELEM_HIGHLIGHT_BG]  = 14;  // Yellow
    t0.colors[ELEM_STATUS_FG]     = 14;  // Yellow
    t0.colors[ELEM_STATUS_BG]     = 0;   // Black
    t0.colors[ELEM_BORDER_FG]     = 8;   // DkGray
    t0.colors[ELEM_BORDER_BG]     = 0;   // Black

    // Dark
    Theme& t1 = themes[theme_count++];
    neo_strcpy(t1.name, "Dark");
    t1.colors[ELEM_TITLE_FG]     = 15;
    t1.colors[ELEM_TITLE_BG]     = 8;
    t1.colors[ELEM_MENU_FG]      = 7;
    t1.colors[ELEM_MENU_BG]      = 0;
    t1.colors[ELEM_TEXT_FG]       = 8;
    t1.colors[ELEM_TEXT_BG]       = 0;
    t1.colors[ELEM_HIGHLIGHT_FG]  = 15;
    t1.colors[ELEM_HIGHLIGHT_BG]  = 8;
    t1.colors[ELEM_STATUS_FG]     = 7;
    t1.colors[ELEM_STATUS_BG]     = 0;
    t1.colors[ELEM_BORDER_FG]     = 8;
    t1.colors[ELEM_BORDER_BG]     = 0;

    // Light
    Theme& t2 = themes[theme_count++];
    neo_strcpy(t2.name, "Light");
    t2.colors[ELEM_TITLE_FG]     = 1;
    t2.colors[ELEM_TITLE_BG]     = 15;
    t2.colors[ELEM_MENU_FG]      = 0;
    t2.colors[ELEM_MENU_BG]      = 7;
    t2.colors[ELEM_TEXT_FG]       = 0;
    t2.colors[ELEM_TEXT_BG]       = 7;
    t2.colors[ELEM_HIGHLIGHT_FG]  = 15;
    t2.colors[ELEM_HIGHLIGHT_BG]  = 1;
    t2.colors[ELEM_STATUS_FG]     = 0;
    t2.colors[ELEM_STATUS_BG]     = 7;
    t2.colors[ELEM_BORDER_FG]     = 8;
    t2.colors[ELEM_BORDER_BG]     = 7;

    // Retro Green
    Theme& t3 = themes[theme_count++];
    neo_strcpy(t3.name, "Retro Green");
    t3.colors[ELEM_TITLE_FG]     = 10;
    t3.colors[ELEM_TITLE_BG]     = 0;
    t3.colors[ELEM_MENU_FG]      = 10;
    t3.colors[ELEM_MENU_BG]      = 0;
    t3.colors[ELEM_TEXT_FG]       = 2;
    t3.colors[ELEM_TEXT_BG]       = 0;
    t3.colors[ELEM_HIGHLIGHT_FG]  = 0;
    t3.colors[ELEM_HIGHLIGHT_BG]  = 10;
    t3.colors[ELEM_STATUS_FG]     = 10;
    t3.colors[ELEM_STATUS_BG]     = 0;
    t3.colors[ELEM_BORDER_FG]     = 2;
    t3.colors[ELEM_BORDER_BG]     = 0;

    // Amber
    Theme& t4 = themes[theme_count++];
    neo_strcpy(t4.name, "Amber");
    t4.colors[ELEM_TITLE_FG]     = 14;
    t4.colors[ELEM_TITLE_BG]     = 0;
    t4.colors[ELEM_MENU_FG]      = 14;
    t4.colors[ELEM_MENU_BG]      = 0;
    t4.colors[ELEM_TEXT_FG]       = 6;
    t4.colors[ELEM_TEXT_BG]       = 0;
    t4.colors[ELEM_HIGHLIGHT_FG]  = 0;
    t4.colors[ELEM_HIGHLIGHT_BG]  = 14;
    t4.colors[ELEM_STATUS_FG]     = 14;
    t4.colors[ELEM_STATUS_BG]     = 0;
    t4.colors[ELEM_BORDER_FG]     = 6;
    t4.colors[ELEM_BORDER_BG]     = 0;

    // Cyberpunk
    Theme& t5 = themes[theme_count++];
    neo_strcpy(t5.name, "Cyberpunk");
    t5.colors[ELEM_TITLE_FG]     = 13;  // Magenta
    t5.colors[ELEM_TITLE_BG]     = 0;
    t5.colors[ELEM_MENU_FG]      = 11;  // Cyan
    t5.colors[ELEM_MENU_BG]      = 0;
    t5.colors[ELEM_TEXT_FG]       = 13;
    t5.colors[ELEM_TEXT_BG]       = 0;
    t5.colors[ELEM_HIGHLIGHT_FG]  = 0;
    t5.colors[ELEM_HIGHLIGHT_BG]  = 13;
    t5.colors[ELEM_STATUS_FG]     = 11;
    t5.colors[ELEM_STATUS_BG]     = 0;
    t5.colors[ELEM_BORDER_FG]     = 5;
    t5.colors[ELEM_BORDER_BG]     = 0;

    // Ocean
    Theme& t6 = themes[theme_count++];
    neo_strcpy(t6.name, "Ocean");
    t6.colors[ELEM_TITLE_FG]     = 15;
    t6.colors[ELEM_TITLE_BG]     = 1;
    t6.colors[ELEM_MENU_FG]      = 11;
    t6.colors[ELEM_MENU_BG]      = 1;
    t6.colors[ELEM_TEXT_FG]       = 15;
    t6.colors[ELEM_TEXT_BG]       = 1;
    t6.colors[ELEM_HIGHLIGHT_FG]  = 14;
    t6.colors[ELEM_HIGHLIGHT_BG]  = 9;
    t6.colors[ELEM_STATUS_FG]     = 11;
    t6.colors[ELEM_STATUS_BG]     = 1;
    t6.colors[ELEM_BORDER_FG]     = 3;
    t6.colors[ELEM_BORDER_BG]     = 1;
}

static Theme& cur() { return themes[current_theme]; }

// --- Preview window ---
static void draw_preview(int start_row) {
    Theme& t = cur();
    int w = neo::display::get_width();
    int pw = 50;
    int px = (w - pw) / 2;

    // Title bar
    neo::display::set_cursor(px, start_row);
    neo::display::set_color(t.colors[ELEM_TITLE_FG], t.colors[ELEM_TITLE_BG]);
    for (int i = 0; i < pw; i++) neo::display::putchar(' ');
    neo::display::set_cursor(px + 2, start_row);
    neo::display::printf("NeoApp - Preview Window");
    neo::display::set_cursor(px + pw - 4, start_row);
    neo::display::printf("[X]");

    // Border top
    neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
    neo::display::set_cursor(px, start_row + 1);
    neo::display::putchar('+');
    for (int i = 0; i < pw - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    // Menu bar
    neo::display::set_cursor(px, start_row + 2);
    neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
    neo::display::putchar('|');
    neo::display::set_color(t.colors[ELEM_MENU_FG], t.colors[ELEM_MENU_BG]);
    neo::display::printf(" File  Edit  View  Help");
    for (int i = 25; i < pw - 1; i++) neo::display::putchar(' ');
    neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
    neo::display::putchar('|');

    // Separator
    neo::display::set_cursor(px, start_row + 3);
    neo::display::putchar('|');
    for (int i = 0; i < pw - 2; i++) neo::display::putchar('-');
    neo::display::putchar('|');

    // Text area
    const char* sample_lines[] = {
        "  Welcome to NeoTheme!",
        "  This is a preview of the",
        "  current color scheme.",
        "",
        "  Selected text looks like",
    };

    for (int line = 0; line < 5; line++) {
        neo::display::set_cursor(px, start_row + 4 + line);
        neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
        neo::display::putchar('|');
        neo::display::set_color(t.colors[ELEM_TEXT_FG], t.colors[ELEM_TEXT_BG]);

        const char* txt = sample_lines[line];
        int tlen = neo_strlen(txt);
        neo::display::printf("%s", txt);
        for (int i = tlen; i < pw - 2; i++) neo::display::putchar(' ');

        neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
        neo::display::putchar('|');
    }

    // Highlighted text line
    neo::display::set_cursor(px, start_row + 9);
    neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
    neo::display::putchar('|');
    neo::display::set_color(t.colors[ELEM_TEXT_FG], t.colors[ELEM_TEXT_BG]);
    neo::display::printf("  ");
    neo::display::set_color(t.colors[ELEM_HIGHLIGHT_FG], t.colors[ELEM_HIGHLIGHT_BG]);
    neo::display::printf("  this highlighted text  ");
    neo::display::set_color(t.colors[ELEM_TEXT_FG], t.colors[ELEM_TEXT_BG]);
    for (int i = 29; i < pw - 2; i++) neo::display::putchar(' ');
    neo::display::set_color(t.colors[ELEM_BORDER_FG], t.colors[ELEM_BORDER_BG]);
    neo::display::putchar('|');

    // Border bottom
    neo::display::set_cursor(px, start_row + 10);
    neo::display::putchar('+');
    for (int i = 0; i < pw - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    // Status bar
    neo::display::set_cursor(px, start_row + 11);
    neo::display::set_color(t.colors[ELEM_STATUS_FG], t.colors[ELEM_STATUS_BG]);
    neo::display::printf(" Ready | Theme: %-20s | Line 1, Col 1 ", t.name);
    for (int i = 48; i < pw; i++) neo::display::putchar(' ');

    neo::display::set_color(7, 0);
}

// --- Color palette display ---
static void draw_palette(int start_row) {
    neo::display::set_cursor(2, start_row);
    neo::display::set_fg(15);
    neo::display::printf("Color Palette:");
    neo::display::set_fg(7);

    neo::display::set_cursor(2, start_row + 1);
    for (int c = 0; c < 16; c++) {
        neo::display::set_color(15, c);
        neo::display::printf(" %2d ", c);
    }
    neo::display::set_color(7, 0);

    neo::display::set_cursor(2, start_row + 2);
    for (int c = 0; c < 16; c++) {
        neo::display::printf("%-4s", color_names[c]);
    }
}

// --- Element editor ---
static void draw_element_list(int start_row) {
    neo::display::set_cursor(2, start_row);
    neo::display::set_fg(15);
    neo::display::printf("Theme Elements:");
    neo::display::set_fg(7);

    Theme& t = cur();

    for (int i = 0; i < MAX_ELEMENTS; i++) {
        neo::display::set_cursor(2, start_row + 1 + i);
        if (i == selected_element) {
            neo::display::set_color(0, 14);
        } else {
            neo::display::set_color(7, 0);
        }
        neo::display::printf(" %-12s = %2d (%-10s) ", element_names[i], t.colors[i],
            color_names[t.colors[i]]);

        // Color swatch
        neo::display::set_color(t.colors[i], 0);
        neo::display::printf("###");
        neo::display::set_color(7, 0);
        neo::display::clear_eol();
    }
}

// --- Save theme ---
static void save_theme(const char* path) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) {
        kprintf("NeoTheme: Cannot save to %s\n", path);
        return;
    }

    Theme& t = cur();
    // Simple text format: name\ncolor0\ncolor1\n...
    char buf[512];
    int pos = 0;

    // Name line
    int nlen = neo_strlen(t.name);
    neo_memcpy(buf + pos, t.name, nlen);
    pos += nlen;
    buf[pos++] = '\n';

    // Color values
    for (int i = 0; i < MAX_ELEMENTS; i++) {
        buf[pos++] = '0' + (t.colors[i] / 10);
        buf[pos++] = '0' + (t.colors[i] % 10);
        buf[pos++] = '\n';
    }

    neo::filesystem::write(fh, (unsigned char*)buf, pos);
    neo::filesystem::close(fh);
}

// --- Load theme ---
static bool load_theme(const char* path) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) return false;

    char buf[512];
    int bytes = neo::filesystem::read(fh, (unsigned char*)buf, 511);
    neo::filesystem::close(fh);
    if (bytes <= 0) return false;
    buf[bytes] = 0;

    // Parse: first line is name
    if (theme_count >= MAX_THEMES) return false;

    Theme& t = themes[theme_count];
    int pos = 0;
    int ni = 0;
    while (buf[pos] && buf[pos] != '\n' && ni < 31) {
        t.name[ni++] = buf[pos++];
    }
    t.name[ni] = 0;
    if (buf[pos] == '\n') pos++;

    // Read color values
    for (int i = 0; i < MAX_ELEMENTS && buf[pos]; i++) {
        int val = 0;
        while (buf[pos] >= '0' && buf[pos] <= '9') {
            val = val * 10 + (buf[pos] - '0');
            pos++;
        }
        if (val > 15) val = 15;
        t.colors[i] = (unsigned char)val;
        if (buf[pos] == '\n') pos++;
    }

    theme_count++;
    return true;
}

// --- Theme selector ---
static void theme_selector() {
    while (true) {
        neo::display::clear();
        neo::display::set_color(14, 1);
        int w = neo::display::get_width();
        neo::display::set_cursor(0, 0);
        for (int i = 0; i < w; i++) neo::display::putchar(' ');
        neo::display::set_cursor(2, 0);
        neo::display::printf("NeoTheme v1.0 - Theme Engine");
        neo::display::set_color(7, 0);

        // Theme list
        neo::display::set_cursor(2, 2);
        neo::display::set_fg(15);
        neo::display::printf("Available Themes:");
        neo::display::set_fg(7);

        for (int i = 0; i < theme_count; i++) {
            neo::display::set_cursor(2, 3 + i);
            if (i == current_theme) {
                neo::display::set_color(0, 14);
                neo::display::printf(" > %-20s ", themes[i].name);
                neo::display::set_color(7, 0);
            } else {
                neo::display::printf("   %-20s", themes[i].name);
            }
        }

        // Preview
        draw_preview(3);

        // Element editor on right or below
        int elem_start = 3 + theme_count + 1;
        draw_element_list(elem_start);

        // Palette
        draw_palette(elem_start + MAX_ELEMENTS + 2);

        // Controls
        int ctrl_row = elem_start + MAX_ELEMENTS + 6;
        neo::display::set_cursor(0, ctrl_row);
        neo::display::set_fg(14);
        neo::display::printf("  [T] Select theme  [Up/Down] Element  [+/-] Change color");
        neo::display::printf("\n  [S] Save  [L] Load  [N] New theme  [D] Duplicate  [Q] Quit");
        neo::display::set_fg(7);

        // Input
        while (!neo::keyboard::key_available()) neo::proc::yield();
        unsigned char sc = neo::keyboard::read_scancode();
        char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());

        switch (ch) {
            case 'q': case 'Q': return;

            case 't': case 'T':
                current_theme = (current_theme + 1) % theme_count;
                break;

            case 'j': case 'J':  // Down
                selected_element = (selected_element + 1) % MAX_ELEMENTS;
                break;

            case 'k': case 'K':  // Up
                selected_element = (selected_element + MAX_ELEMENTS - 1) % MAX_ELEMENTS;
                break;

            case '+': case '=': {
                Theme& t = cur();
                t.colors[selected_element] = (t.colors[selected_element] + 1) & 0x0F;
                break;
            }

            case '-': case '_': {
                Theme& t = cur();
                t.colors[selected_element] = (t.colors[selected_element] + 15) & 0x0F;
                break;
            }

            case 's': case 'S': {
                char path[INODE_SIZE];
                neo::display::set_cursor(2, ctrl_row + 2);
                neo::display::printf("Save to: ");
                neo::console::getline(path, sizeof(path), nullptr);
                if (path[0]) {
                    save_theme(path);
                    neo::display::set_fg(10);
                    neo::display::printf("  Saved!\n");
                    neo::display::set_fg(7);
                    neo::timer::delay_ms(500);
                }
                break;
            }

            case 'l': case 'L': {
                char path[INODE_SIZE];
                neo::display::set_cursor(2, ctrl_row + 2);
                neo::display::printf("Load from: ");
                neo::console::getline(path, sizeof(path), nullptr);
                if (path[0]) {
                    if (load_theme(path)) {
                        current_theme = theme_count - 1;
                        neo::display::set_fg(10);
                        neo::display::printf("  Loaded!\n");
                    } else {
                        neo::display::set_fg(12);
                        neo::display::printf("  Failed to load.\n");
                    }
                    neo::display::set_fg(7);
                    neo::timer::delay_ms(500);
                }
                break;
            }

            case 'n': case 'N': {
                if (theme_count >= MAX_THEMES) break;
                char name[32];
                neo::display::set_cursor(2, ctrl_row + 2);
                neo::display::printf("New theme name: ");
                neo::console::getline(name, sizeof(name), nullptr);
                if (name[0]) {
                    Theme& nt = themes[theme_count];
                    neo_strcpy(nt.name, name);
                    // Start with default colors
                    neo_memcpy(nt.colors, themes[0].colors, MAX_ELEMENTS);
                    current_theme = theme_count;
                    theme_count++;
                }
                break;
            }

            case 'd': case 'D': {
                if (theme_count >= MAX_THEMES) break;
                Theme& src = cur();
                Theme& dst = themes[theme_count];
                neo_memcpy(&dst, &src, sizeof(Theme));
                int nlen = neo_strlen(dst.name);
                if (nlen < 28) {
                    neo_strcat(dst.name, " Copy");
                }
                current_theme = theme_count;
                theme_count++;
                break;
            }
        }
    }
}

}  // namespace neotheme

extern "C" void app_main(int argc, char** argv) {
    neotheme::init_themes();
    neotheme::current_theme = 0;
    neotheme::selected_element = 0;

    if (argc > 1 && neo_strcmp(argv[1], "load") == 0 && argc > 2) {
        neotheme::load_theme(argv[2]);
        neotheme::current_theme = neotheme::theme_count - 1;
    }

    neotheme::theme_selector();

    // Reset colors on exit
    neo::display::set_color(7, 0);
}
