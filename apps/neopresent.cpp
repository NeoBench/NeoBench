#include "../include/neobench.h"
#include "../lib/string.h"

// NeoPresent - Presentation Tool for NeoBench
// Features: slides with title/bullets, slideshow mode, transitions, sorter view, save/load

namespace neopresent {

static const int MAX_SLIDES = 50;
static const int MAX_BULLETS = 12;
static const int MAX_TEXT = 128;
static const int MAX_TITLE = 80;

struct Slide {
    char title[MAX_TITLE];
    char bullets[MAX_BULLETS][MAX_TEXT];
    int bullet_count;
    int bg_color;
    int fg_color;
};

enum ViewMode { VIEW_EDITOR, VIEW_SLIDESHOW, VIEW_SORTER };
enum TransitionType { TRANS_NONE, TRANS_WIPE_DOWN, TRANS_WIPE_RIGHT, TRANS_DISSOLVE };

static Slide slides[MAX_SLIDES];
static int slide_count;
static int current_slide;
static ViewMode view_mode;
static TransitionType transition;
static int screen_w, screen_h;
static bool running;
static char filename[64];
static bool modified;

// Editor state
static int edit_field; // 0=title, 1..N=bullet
static int edit_pos;
static bool editing;
static char edit_buf[MAX_TEXT];

static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char* d, const char* s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}

static void init_presentation() {
    slide_count = 1;
    current_slide = 0;
    view_mode = VIEW_EDITOR;
    transition = TRANS_WIPE_DOWN;
    editing = false;
    filename[0] = 0;
    modified = false;

    for (int i = 0; i < MAX_SLIDES; i++) {
        slides[i].title[0] = 0;
        slides[i].bullet_count = 0;
        slides[i].bg_color = 0x111133;
        slides[i].fg_color = 0xFFFFFF;
        for (int j = 0; j < MAX_BULLETS; j++)
            slides[i].bullets[j][0] = 0;
    }

    str_copy(slides[0].title, "Welcome to NeoPresent", MAX_TITLE);
    str_copy(slides[0].bullets[0], "Create slides with titles and bullet points", MAX_TEXT);
    str_copy(slides[0].bullets[1], "Press F5 to start slideshow", MAX_TEXT);
    str_copy(slides[0].bullets[2], "Press F7 for slide sorter view", MAX_TEXT);
    slides[0].bullet_count = 3;
}

// === Transition Effects ===

static void transition_wipe_down(int bg_color) {
    for (int y = 0; y < screen_h; y++) {
        neo::display::set_cursor(0, y);
        neo::display::set_color(0, bg_color);
        for (int x = 0; x < screen_w; x++) neo::display::putchar(' ');
        neo::timer::delay_ms(15);
    }
}

static void transition_wipe_right(int bg_color) {
    for (int x = 0; x < screen_w; x += 2) {
        for (int y = 0; y < screen_h; y++) {
            neo::display::set_cursor(x, y);
            neo::display::set_color(0, bg_color);
            neo::display::putchar(' ');
            if (x + 1 < screen_w) neo::display::putchar(' ');
        }
        neo::timer::delay_ms(5);
    }
}

static void transition_dissolve(int bg_color) {
    // Pseudo-random dissolve using LCG
    unsigned int seed = neo::timer::get_ticks();
    int total = screen_w * screen_h;
    int step = total / 8;
    for (int pass = 0; pass < 8; pass++) {
        for (int i = pass; i < total; i += 8) {
            seed = seed * 1103515245 + 12345;
            int pos = (seed >> 16) % total;
            int px = pos % screen_w;
            int py = pos / screen_w;
            neo::display::set_cursor(px, py);
            neo::display::set_color(0, bg_color);
            neo::display::putchar(' ');
        }
        neo::timer::delay_ms(40);
    }
}

static void do_transition(int bg_color) {
    switch (transition) {
        case TRANS_WIPE_DOWN: transition_wipe_down(bg_color); break;
        case TRANS_WIPE_RIGHT: transition_wipe_right(bg_color); break;
        case TRANS_DISSOLVE: transition_dissolve(bg_color); break;
        default: neo::display::clear(); break;
    }
}

// === Drawing ===

static void draw_centered(const char* text, int y, int fg, int bg, bool bold) {
    int len = str_len(text);
    int x = (screen_w - len) / 2;
    if (x < 0) x = 0;
    neo::display::set_cursor(x, y);
    neo::display::set_color(fg, bg);
    neo::display::set_bold(bold);
    neo::display::puts(text);
    neo::display::set_bold(false);
}

static void draw_slideshow_slide() {
    Slide* s = &slides[current_slide];
    int bg = s->bg_color;
    int fg = s->fg_color;

    // Background
    neo::display::set_color(fg, bg);
    for (int y = 0; y < screen_h; y++) {
        neo::display::set_cursor(0, y);
        for (int x = 0; x < screen_w; x++) neo::display::putchar(' ');
    }

    // Top border
    neo::display::set_cursor(2, 1);
    neo::display::set_color(0x5577BB, bg);
    for (int i = 0; i < screen_w - 4; i++) neo::display::putchar('=');

    // Title
    draw_centered(s->title, 3, 0xFFFF88, bg, true);

    // Separator
    neo::display::set_cursor(4, 5);
    neo::display::set_color(0x5577BB, bg);
    for (int i = 0; i < screen_w - 8; i++) neo::display::putchar('-');

    // Bullets
    int y = 7;
    for (int i = 0; i < s->bullet_count && y < screen_h - 3; i++) {
        neo::display::set_cursor(6, y);
        neo::display::set_color(0x88BBFF, bg);
        neo::display::puts("  * ");
        neo::display::set_color(fg, bg);
        neo::display::puts(s->bullets[i]);
        y += 2;
    }

    // Bottom info
    neo::display::set_cursor(2, screen_h - 2);
    neo::display::set_color(0x5577BB, bg);
    for (int i = 0; i < screen_w - 4; i++) neo::display::putchar('=');

    char info[64];
    ksprintf(info, 64, "Slide %d of %d", current_slide + 1, slide_count);
    draw_centered(info, screen_h - 1, 0x888888, bg, false);
}

static void draw_editor() {
    Slide* s = &slides[current_slide];

    // Title bar
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0x000000, 0x5599DD);
    neo::display::set_bold(true);
    neo::display::puts(" NeoPresent ");
    neo::display::set_bold(false);
    if (filename[0]) neo::display::puts(filename);
    else neo::display::puts("[Untitled]");
    if (modified) neo::display::puts(" *");
    char sl[32];
    ksprintf(sl, 32, "  Slide %d/%d", current_slide + 1, slide_count);
    neo::display::puts(sl);
    neo::display::clear_eol();

    // Menu
    neo::display::set_cursor(0, 1);
    neo::display::set_color(0xAAAAAA, 0x333355);
    neo::display::puts(" F2:Save F3:Load F4:New F5:Show F6:Del F7:Sorter F8:Trans PgUp/Dn:Nav ESC:Quit ");
    neo::display::clear_eol();

    // Slide preview area
    int preview_top = 3;
    neo::display::set_cursor(2, preview_top);
    neo::display::set_color(0x5577BB, 0x111133);
    neo::display::putchar('+');
    for (int i = 0; i < screen_w - 6; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    // Title field
    int y = preview_top + 1;
    neo::display::set_cursor(2, y);
    neo::display::set_color(0x5577BB, 0x111133);
    neo::display::putchar('|');
    neo::display::set_cursor(screen_w - 3, y);
    neo::display::putchar('|');

    bool title_active = (edit_field == 0 && editing);
    neo::display::set_cursor(4, y);
    neo::display::set_color(0x888888, 0x111133);
    neo::display::puts("Title: ");
    if (title_active) {
        neo::display::set_color(0xFFFF88, 0x222255);
        neo::display::puts(edit_buf);
    } else {
        neo::display::set_color(edit_field == 0 ? 0xFFFF88 : 0xFFFFFF, 0x111133);
        neo::display::set_bold(true);
        neo::display::puts(s->title);
        neo::display::set_bold(false);
    }
    neo::display::clear_eol();
    neo::display::set_cursor(screen_w - 3, y);
    neo::display::set_color(0x5577BB, 0x111133);
    neo::display::putchar('|');

    // Separator
    y++;
    neo::display::set_cursor(2, y);
    neo::display::set_color(0x5577BB, 0x111133);
    neo::display::putchar('|');
    for (int i = 0; i < screen_w - 6; i++) neo::display::putchar('-');
    neo::display::putchar('|');

    // Bullets
    for (int i = 0; i < MAX_BULLETS && y + 1 < screen_h - 4; i++) {
        y++;
        neo::display::set_cursor(2, y);
        neo::display::set_color(0x5577BB, 0x111133);
        neo::display::putchar('|');
        neo::display::set_cursor(screen_w - 3, y);
        neo::display::putchar('|');

        neo::display::set_cursor(4, y);
        bool bullet_active = (edit_field == i + 1 && editing);

        if (i < s->bullet_count) {
            neo::display::set_color(0x88BBFF, 0x111133);
            char bnum[8];
            ksprintf(bnum, 8, "%2d. ", i + 1);
            neo::display::puts(bnum);
            if (bullet_active) {
                neo::display::set_color(0xFFFF88, 0x222255);
                neo::display::puts(edit_buf);
            } else {
                neo::display::set_color(edit_field == i + 1 ? 0xFFFF88 : 0xDDDDDD, 0x111133);
                neo::display::puts(s->bullets[i]);
            }
        } else if (i == s->bullet_count) {
            neo::display::set_color(0x555555, 0x111133);
            neo::display::puts("    [Enter to add bullet]");
        }
        neo::display::clear_eol();
        neo::display::set_cursor(screen_w - 3, y);
        neo::display::set_color(0x5577BB, 0x111133);
        neo::display::putchar('|');
    }

    // Bottom border
    y++;
    neo::display::set_cursor(2, y);
    neo::display::set_color(0x5577BB, 0x111133);
    neo::display::putchar('+');
    for (int i = 0; i < screen_w - 6; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    // Fill remaining
    for (int fy = y + 1; fy < screen_h - 1; fy++) {
        neo::display::set_cursor(0, fy);
        neo::display::set_color(0, 0x111133);
        neo::display::clear_eol();
    }

    // Status
    neo::display::set_cursor(0, screen_h - 1);
    neo::display::set_color(0x000000, 0x55AA55);
    const char* trans_names[] = { "None", "Wipe Down", "Wipe Right", "Dissolve" };
    char status[128];
    ksprintf(status, 128, " Field: %s | Transition: %s | Bullets: %d ",
             edit_field == 0 ? "Title" : "Bullet",
             trans_names[transition], s->bullet_count);
    neo::display::puts(status);
    neo::display::clear_eol();
}

static void draw_sorter() {
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0x000000, 0x5599DD);
    neo::display::set_bold(true);
    neo::display::puts(" NeoPresent - Slide Sorter ");
    neo::display::set_bold(false);
    neo::display::clear_eol();

    neo::display::set_cursor(0, 1);
    neo::display::set_color(0xAAAAAA, 0x333355);
    neo::display::puts(" Arrows:Select  Enter:Edit  M:Move  D:Delete  N:New  ESC:Back ");
    neo::display::clear_eol();

    int cols = (screen_w - 2) / 22;
    if (cols < 1) cols = 1;
    int thumb_w = 20;
    int thumb_h = 6;

    for (int i = 0; i < slide_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int sx = 2 + col * (thumb_w + 2);
        int sy = 3 + row * (thumb_h + 1);
        if (sy + thumb_h >= screen_h - 1) break;

        bool selected = (i == current_slide);
        int border_color = selected ? 0xFFFF88 : 0x555577;
        int bg = selected ? 0x222255 : 0x111133;

        // Border
        neo::display::set_cursor(sx, sy);
        neo::display::set_color(border_color, 0x111122);
        neo::display::putchar('+');
        for (int x = 0; x < thumb_w - 2; x++) neo::display::putchar('-');
        neo::display::putchar('+');

        // Slide number
        neo::display::set_cursor(sx, sy + 1);
        neo::display::set_color(border_color, bg);
        neo::display::putchar('|');
        char num[8];
        ksprintf(num, 8, " #%d", i + 1);
        neo::display::puts(num);
        int pad = thumb_w - 2 - str_len(num);
        for (int p = 0; p < pad; p++) neo::display::putchar(' ');
        neo::display::putchar('|');

        // Title (truncated)
        neo::display::set_cursor(sx, sy + 2);
        neo::display::set_color(border_color, bg);
        neo::display::putchar('|');
        neo::display::set_color(0xFFFF88, bg);
        int tlen = str_len(slides[i].title);
        for (int t = 0; t < thumb_w - 2; t++) {
            if (t < tlen) neo::display::putchar(slides[i].title[t]);
            else neo::display::putchar(' ');
        }
        neo::display::set_color(border_color, bg);
        neo::display::putchar('|');

        // Bullet count
        neo::display::set_cursor(sx, sy + 3);
        neo::display::putchar('|');
        neo::display::set_color(0x888888, bg);
        char binfo[20];
        ksprintf(binfo, 20, " %d bullets", slides[i].bullet_count);
        neo::display::puts(binfo);
        pad = thumb_w - 2 - str_len(binfo);
        for (int p = 0; p < pad; p++) neo::display::putchar(' ');
        neo::display::set_color(border_color, bg);
        neo::display::putchar('|');

        // Empty line
        neo::display::set_cursor(sx, sy + 4);
        neo::display::putchar('|');
        for (int x = 0; x < thumb_w - 2; x++) neo::display::putchar(' ');
        neo::display::putchar('|');

        // Bottom border
        neo::display::set_cursor(sx, sy + 5);
        neo::display::putchar('+');
        for (int x = 0; x < thumb_w - 2; x++) neo::display::putchar('-');
        neo::display::putchar('+');
    }
}

static void start_editing() {
    editing = true;
    Slide* s = &slides[current_slide];
    if (edit_field == 0) {
        str_copy(edit_buf, s->title, MAX_TEXT);
    } else {
        int bi = edit_field - 1;
        if (bi < s->bullet_count)
            str_copy(edit_buf, s->bullets[bi], MAX_TEXT);
        else
            edit_buf[0] = 0;
    }
    edit_pos = str_len(edit_buf);
}

static void finish_editing() {
    editing = false;
    Slide* s = &slides[current_slide];
    if (edit_field == 0) {
        str_copy(s->title, edit_buf, MAX_TITLE);
    } else {
        int bi = edit_field - 1;
        if (bi >= s->bullet_count && edit_buf[0]) {
            s->bullet_count = bi + 1;
        }
        str_copy(s->bullets[bi], edit_buf, MAX_TEXT);
    }
    modified = true;
}

static void new_slide() {
    if (slide_count >= MAX_SLIDES) return;
    int ins = current_slide + 1;
    for (int i = slide_count; i > ins; i--) {
        neo_memcpy(&slides[i], &slides[i - 1], sizeof(Slide));
    }
    slides[ins].title[0] = 0;
    slides[ins].bullet_count = 0;
    for (int j = 0; j < MAX_BULLETS; j++) slides[ins].bullets[j][0] = 0;
    slides[ins].bg_color = 0x111133;
    slides[ins].fg_color = 0xFFFFFF;
    slide_count++;
    current_slide = ins;
    edit_field = 0;
    modified = true;
}

static void delete_slide() {
    if (slide_count <= 1) return;
    for (int i = current_slide; i < slide_count - 1; i++) {
        neo_memcpy(&slides[i], &slides[i + 1], sizeof(Slide));
    }
    slide_count--;
    if (current_slide >= slide_count) current_slide = slide_count - 1;
    modified = true;
}

static void move_slide(int dir) {
    int target = current_slide + dir;
    if (target < 0 || target >= slide_count) return;
    Slide tmp;
    neo_memcpy(&tmp, &slides[current_slide], sizeof(Slide));
    neo_memcpy(&slides[current_slide], &slides[target], sizeof(Slide));
    neo_memcpy(&slides[target], &tmp, sizeof(Slide));
    current_slide = target;
    modified = true;
}

static void do_save() {
    if (filename[0] == 0) {
        neo::display::set_cursor(0, screen_h - 1);
        neo::display::set_color(0xFFFFFF, 0x444488);
        neo::display::puts("Save as: ");
        neo::display::clear_eol();
        int pos = 0;
        while (true) {
            if (neo::keyboard::key_available()) {
                int sc = neo::keyboard::read_scancode();
                bool sh = neo::keyboard::is_shift_down();
                char ch = neo::keyboard::translate(sc, sh);
                if (ch == '\n' || ch == '\r') break;
                if (sc == 0x01) return;
                if ((ch == 8 || sc == 0x0E) && pos > 0) { pos--; filename[pos] = 0; neo::display::cursor_left(1); neo::display::putchar(' '); neo::display::cursor_left(1); }
                else if (ch >= ' ' && pos < 60) { filename[pos++] = ch; filename[pos] = 0; neo::display::putchar(ch); }
            }
            neo::proc::yield();
        }
    }
    if (filename[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) return;

    char hdr[32];
    ksprintf(hdr, 32, "NEOPRESENT\n%d\n", slide_count);
    neo::filesystem::write(fh, hdr, str_len(hdr));

    for (int i = 0; i < slide_count; i++) {
        char line[INODE_SIZE];
        ksprintf(line, INODE_SIZE, "SLIDE\n%s\n%d\n", slides[i].title, slides[i].bullet_count);
        neo::filesystem::write(fh, line, str_len(line));
        for (int j = 0; j < slides[i].bullet_count; j++) {
            neo::filesystem::write(fh, slides[i].bullets[j], str_len(slides[i].bullets[j]));
            char nl = '\n';
            neo::filesystem::write(fh, &nl, 1);
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
    int pos = 0; fname[0] = 0;
    while (true) {
        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            bool sh = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, sh);
            if (ch == '\n' || ch == '\r') break;
            if (sc == 0x01) return;
            if ((ch == 8 || sc == 0x0E) && pos > 0) { pos--; fname[pos] = 0; neo::display::cursor_left(1); neo::display::putchar(' '); neo::display::cursor_left(1); }
            else if (ch >= ' ' && pos < 60) { fname[pos++] = ch; fname[pos] = 0; neo::display::putchar(ch); }
        }
        neo::proc::yield();
    }
    if (fname[0] == 0) return;

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, fname, neo::filesystem::MODE_READ) != 0) return;

    // Simple line-based parser
    char buf[2048];
    int n = neo::filesystem::read(fh, buf, 2048);
    neo::filesystem::close(fh);
    if (n <= 0) return;
    buf[n] = 0;

    // Parse header
    int p = 0;
    // Skip "NEOPRESENT\n"
    while (p < n && buf[p] != '\n') p++;
    p++;
    // Read slide count
    int sc = 0;
    while (p < n && buf[p] >= '0' && buf[p] <= '9') { sc = sc * 10 + (buf[p] - '0'); p++; }
    if (buf[p] == '\n') p++;

    init_presentation();
    str_copy(filename, fname, 64);
    slide_count = 0;

    while (p < n && slide_count < MAX_SLIDES) {
        // Expect "SLIDE\n"
        while (p < n && buf[p] != 'S') p++;
        if (p >= n) break;
        while (p < n && buf[p] != '\n') p++;
        p++;

        // Title
        int ti = 0;
        while (p < n && buf[p] != '\n' && ti < MAX_TITLE - 1) {
            slides[slide_count].title[ti++] = buf[p++];
        }
        slides[slide_count].title[ti] = 0;
        if (buf[p] == '\n') p++;

        // Bullet count
        int bc = 0;
        while (p < n && buf[p] >= '0' && buf[p] <= '9') { bc = bc * 10 + (buf[p] - '0'); p++; }
        if (buf[p] == '\n') p++;
        slides[slide_count].bullet_count = bc;

        for (int b = 0; b < bc && b < MAX_BULLETS; b++) {
            int bi = 0;
            while (p < n && buf[p] != '\n' && bi < MAX_TEXT - 1) {
                slides[slide_count].bullets[b][bi++] = buf[p++];
            }
            slides[slide_count].bullets[b][bi] = 0;
            if (buf[p] == '\n') p++;
        }
        slide_count++;
    }
    if (slide_count == 0) slide_count = 1;
    current_slide = 0;
}

static void cycle_transition() {
    transition = (TransitionType)(((int)transition + 1) % 4);
}

static void handle_editor_key(int scancode) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(scancode, shift);
    Slide* s = &slides[current_slide];

    if (editing) {
        if (ch == '\n' || ch == '\r') { finish_editing(); return; }
        if (scancode == 0x01) { editing = false; return; }
        if ((ch == 8 || scancode == 0x0E) && edit_pos > 0) {
            edit_pos--; edit_buf[edit_pos] = 0; return;
        }
        if (ch >= ' ' && ch < 127 && edit_pos < MAX_TEXT - 1) {
            edit_buf[edit_pos++] = ch; edit_buf[edit_pos] = 0; return;
        }
        return;
    }

    switch (scancode) {
        case 0x01: running = false; return;
        case 0x3C: do_save(); return;
        case 0x3D: do_load(); return;
        case 0x3E: new_slide(); return; // F4
        case 0x3F: // F5 slideshow
            view_mode = VIEW_SLIDESHOW;
            neo::display::clear();
            do_transition(slides[current_slide].bg_color);
            draw_slideshow_slide();
            return;
        case 0x40: delete_slide(); return; // F6
        case 0x41: view_mode = VIEW_SORTER; neo::display::clear(); draw_sorter(); return; // F7
        case 0x42: cycle_transition(); return; // F8

        case 0x48: // Up
            if (edit_field > 0) edit_field--;
            return;
        case 0x50: // Down
            if (edit_field < s->bullet_count) edit_field++;
            return;
        case 0x49: // PgUp
            if (current_slide > 0) { current_slide--; edit_field = 0; }
            return;
        case 0x51: // PgDn
            if (current_slide < slide_count - 1) { current_slide++; edit_field = 0; }
            return;
    }

    if (ch == '\n' || ch == '\r') {
        start_editing();
        return;
    }
}

static void handle_slideshow_key(int scancode) {
    switch (scancode) {
        case 0x01: // ESC
            view_mode = VIEW_EDITOR;
            neo::display::clear();
            return;
        case 0x4D: // Right
        case 0x50: // Down
        case 0x51: // PgDn
            if (current_slide < slide_count - 1) {
                current_slide++;
                do_transition(slides[current_slide].bg_color);
                draw_slideshow_slide();
            }
            return;
        case 0x4B: // Left
        case 0x48: // Up
        case 0x49: // PgUp
            if (current_slide > 0) {
                current_slide--;
                do_transition(slides[current_slide].bg_color);
                draw_slideshow_slide();
            }
            return;
    }
    char ch = neo::keyboard::translate(scancode, false);
    if (ch == ' ' || ch == '\n' || ch == '\r') {
        if (current_slide < slide_count - 1) {
            current_slide++;
            do_transition(slides[current_slide].bg_color);
            draw_slideshow_slide();
        }
    }
}

static void handle_sorter_key(int scancode) {
    char ch = neo::keyboard::translate(scancode, false);
    int cols = (screen_w - 2) / 22;
    if (cols < 1) cols = 1;

    switch (scancode) {
        case 0x01: view_mode = VIEW_EDITOR; neo::display::clear(); return;
        case 0x4B: if (current_slide > 0) current_slide--; break;
        case 0x4D: if (current_slide < slide_count - 1) current_slide++; break;
        case 0x48: if (current_slide >= cols) current_slide -= cols; break;
        case 0x50: if (current_slide + cols < slide_count) current_slide += cols; break;
    }
    if (ch == '\n' || ch == '\r') { view_mode = VIEW_EDITOR; edit_field = 0; neo::display::clear(); return; }
    if (ch == 'd' || ch == 'D') { delete_slide(); }
    if (ch == 'n' || ch == 'N') { new_slide(); }
    if (ch == 'm' || ch == 'M') { move_slide(1); }

    neo::display::clear();
    draw_sorter();
}

} // namespace neopresent

extern "C" void app_main(int argc, char** argv) {
    using namespace neopresent;

    screen_w = neo::display::get_width();
    screen_h = neo::display::get_height();
    running = true;

    init_presentation();

    if (argc > 1) {
        str_copy(filename, argv[1], 64);
        // Attempt load
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_READ) == 0) {
            neo::filesystem::close(fh);
            do_load();
        }
    }

    neo::display::clear();

    while (running) {
        if (view_mode == VIEW_EDITOR) draw_editor();

        if (neo::keyboard::key_available()) {
            int sc = neo::keyboard::read_scancode();
            switch (view_mode) {
                case VIEW_EDITOR: handle_editor_key(sc); break;
                case VIEW_SLIDESHOW: handle_slideshow_key(sc); break;
                case VIEW_SORTER: handle_sorter_key(sc); break;
            }
        }
        neo::proc::yield();
    }

    neo::display::clear();
    neo::display::set_color(0xDDDDDD, 0x000000);
    neo::display::set_cursor(0, 0);
}
