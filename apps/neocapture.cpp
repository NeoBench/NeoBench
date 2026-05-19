#include "../include/neobench.h"
#include "../lib/string.h"

// NeoCapture - Screenshot & screen capture tool
// Timed capture, region selection, gallery, auto-naming

namespace neocapture {

static const int MAX_CAPTURES = 64;
static const int MAX_REGION_W = 80;
static const int MAX_REGION_H = 50;

struct CaptureCell {
    char ch;
    unsigned char fg;
    unsigned char bg;
};

struct CaptureInfo {
    char filename[64];
    int width;
    int height;
    int region_x, region_y;
    int timestamp; // uptime seconds
    bool valid;
};

struct ScreenBuffer {
    CaptureCell cells[MAX_REGION_H][MAX_REGION_W];
    int width;
    int height;

    void clear() {
        for (int y = 0; y < MAX_REGION_H; y++)
            for (int x = 0; x < MAX_REGION_W; x++) {
                cells[y][x].ch = ' ';
                cells[y][x].fg = 7;
                cells[y][x].bg = 0;
            }
        width = 0;
        height = 0;
    }
};

enum Mode {
    MODE_MENU,
    MODE_REGION_SELECT,
    MODE_TIMED_CAPTURE,
    MODE_GALLERY,
    MODE_PREVIEW
};

struct CaptureApp {
    CaptureInfo captures[MAX_CAPTURES];
    int num_captures;
    int screen_w, screen_h;
    bool running;
    Mode mode;
    char status_msg[64];
    int status_timer;

    // Region selection
    int sel_x1, sel_y1, sel_x2, sel_y2;
    bool selecting;
    int sel_cursor_x, sel_cursor_y;

    // Timed capture
    int countdown_secs;

    // Gallery
    int gallery_scroll;
    int gallery_selected;

    // Preview
    ScreenBuffer preview_buf;

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        num_captures = 0;
        running = true;
        mode = MODE_MENU;
        status_msg[0] = '\0';
        status_timer = 0;
        sel_x1 = sel_y1 = 0;
        sel_x2 = screen_w - 1;
        sel_y2 = screen_h - 1;
        selecting = false;
        sel_cursor_x = 0;
        sel_cursor_y = 0;
        countdown_secs = 5;
        gallery_scroll = 0;
        gallery_selected = 0;
        preview_buf.clear();
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    void generate_filename(char* buf, int bufsize) {
        int uptime = (int)neo::timer::get_uptime_seconds();
        int ticks = (int)neo::timer::get_ticks();
        ksprintf(buf, bufsize, "cap_%d_%d.nsc", uptime, ticks % 1000);
    }

    void capture_full_screen() {
        ScreenBuffer buf;
        buf.width = screen_w;
        buf.height = screen_h;
        // We can't actually read the screen, so we capture a simulated screen
        // In practice, this would hook into the display driver
        // For now, we create a timestamp-labeled capture file
        for (int y = 0; y < screen_h && y < MAX_REGION_H; y++) {
            for (int x = 0; x < screen_w && x < MAX_REGION_W; x++) {
                buf.cells[y][x].ch = ' ';
                buf.cells[y][x].fg = 7;
                buf.cells[y][x].bg = 0;
            }
        }
        // Put capture info text in the buffer
        const char* info = "NeoCapture - Full Screen Capture";
        for (int i = 0; info[i] && i < screen_w; i++) {
            buf.cells[0][i].ch = info[i];
            buf.cells[0][i].fg = 7;
        }

        save_capture(&buf);
    }

    void capture_region(int x1, int y1, int x2, int y2) {
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

        ScreenBuffer buf;
        buf.width = x2 - x1 + 1;
        buf.height = y2 - y1 + 1;
        if (buf.width > MAX_REGION_W) buf.width = MAX_REGION_W;
        if (buf.height > MAX_REGION_H) buf.height = MAX_REGION_H;

        for (int y = 0; y < buf.height; y++) {
            for (int x = 0; x < buf.width; x++) {
                buf.cells[y][x].ch = ' ';
                buf.cells[y][x].fg = 7;
                buf.cells[y][x].bg = 0;
            }
        }

        char info[64];
        ksprintf(info, sizeof(info), "Region: %dx%d from (%d,%d)", buf.width, buf.height, x1, y1);
        for (int i = 0; info[i] && i < buf.width; i++) {
            buf.cells[0][i].ch = info[i];
            buf.cells[0][i].fg = 6;
        }

        if (num_captures < MAX_CAPTURES) {
            captures[num_captures].region_x = x1;
            captures[num_captures].region_y = y1;
        }

        save_capture(&buf);
    }

    void save_capture(ScreenBuffer* buf) {
        if (num_captures >= MAX_CAPTURES) {
            set_status("Capture limit reached!");
            return;
        }

        CaptureInfo& info = captures[num_captures];
        generate_filename(info.filename, sizeof(info.filename));
        info.width = buf->width;
        info.height = buf->height;
        info.timestamp = (int)neo::timer::get_uptime_seconds();
        info.valid = true;

        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, info.filename, 1) == 0) {
            // Header
            char magic[4] = {'N','S','C','1'};
            neo::filesystem::write(fh, magic, 4);
            neo::filesystem::write(fh, (char*)&buf->width, 4);
            neo::filesystem::write(fh, (char*)&buf->height, 4);

            // Color code format: [fg;bg]char
            for (int y = 0; y < buf->height; y++) {
                for (int x = 0; x < buf->width; x++) {
                    char cell[4];
                    cell[0] = buf->cells[y][x].ch;
                    cell[1] = (char)buf->cells[y][x].fg;
                    cell[2] = (char)buf->cells[y][x].bg;
                    neo::filesystem::write(fh, cell, 3);
                }
                char nl = '\n';
                neo::filesystem::write(fh, &nl, 1);
            }
            neo::filesystem::close(fh);
            num_captures++;

            char msg[80];
            ksprintf(msg, sizeof(msg), "Saved: %s (%dx%d)", info.filename, buf->width, buf->height);
            set_status(msg);
        } else {
            set_status("Save failed!");
        }
    }

    void load_capture_preview(int idx) {
        if (idx < 0 || idx >= num_captures) return;
        CaptureInfo& info = captures[idx];

        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, info.filename, 0) == 0) {
            char magic[4];
            int w, h;
            neo::filesystem::read(fh, magic, 4);
            neo::filesystem::read(fh, (char*)&w, 4);
            neo::filesystem::read(fh, (char*)&h, 4);

            preview_buf.clear();
            preview_buf.width = w;
            preview_buf.height = h;

            for (int y = 0; y < h && y < MAX_REGION_H; y++) {
                for (int x = 0; x < w && x < MAX_REGION_W; x++) {
                    char cell[3];
                    neo::filesystem::read(fh, cell, 3);
                    preview_buf.cells[y][x].ch = cell[0];
                    preview_buf.cells[y][x].fg = (unsigned char)cell[1];
                    preview_buf.cells[y][x].bg = (unsigned char)cell[2];
                }
                char nl;
                neo::filesystem::read(fh, &nl, 1);
            }
            neo::filesystem::close(fh);
        }
    }

    void render_menu() {
        neo::display::clear();

        // Header
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoCapture - Screen Capture Tool | %d captures ", num_captures);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Logo / title
        neo::display::set_color(7, 0);
        int cx = screen_w / 2 - 15;
        int cy = 4;
        neo::display::set_cursor(cx, cy);
        neo::display::set_fg(6);
        neo::display::puts("+----------------------------+");
        neo::display::set_cursor(cx, cy + 1);
        neo::display::puts("|      N E O C A P T U R E   |");
        neo::display::set_cursor(cx, cy + 2);
        neo::display::puts("|    Screen Capture Tool     |");
        neo::display::set_cursor(cx, cy + 3);
        neo::display::puts("+----------------------------+");

        // Menu options
        neo::display::set_fg(7);
        int my = cy + 6;
        neo::display::set_cursor(cx + 2, my);
        neo::display::set_fg(3);
        neo::display::puts("1");
        neo::display::set_fg(7);
        neo::display::puts(" - Full Screen Capture");

        neo::display::set_cursor(cx + 2, my + 2);
        neo::display::set_fg(3);
        neo::display::puts("2");
        neo::display::set_fg(7);
        neo::display::puts(" - Region Select & Capture");

        neo::display::set_cursor(cx + 2, my + 4);
        neo::display::set_fg(3);
        neo::display::puts("3");
        neo::display::set_fg(7);
        neo::display::puts(" - Timed Capture (countdown)");

        neo::display::set_cursor(cx + 2, my + 6);
        neo::display::set_fg(3);
        neo::display::puts("4");
        neo::display::set_fg(7);
        neo::display::puts(" - Gallery View");

        neo::display::set_cursor(cx + 2, my + 8);
        neo::display::set_fg(3);
        neo::display::puts("5");
        neo::display::set_fg(7);
        neo::display::puts(" - Set Countdown (current: ");
        char tmp[8];
        ksprintf(tmp, sizeof(tmp), "%ds)", countdown_secs);
        neo::display::puts(tmp);

        neo::display::set_cursor(cx + 2, my + 10);
        neo::display::set_fg(3);
        neo::display::puts("Q");
        neo::display::set_fg(7);
        neo::display::puts(" - Quit");

        // Recent captures
        if (num_captures > 0) {
            neo::display::set_cursor(cx, my + 13);
            neo::display::set_fg(6);
            neo::display::puts("Recent captures:");
            neo::display::set_fg(7);
            int show = num_captures < 5 ? num_captures : 5;
            for (int i = num_captures - show; i < num_captures; i++) {
                neo::display::set_cursor(cx + 2, my + 14 + (i - (num_captures - show)));
                char line[80];
                ksprintf(line, sizeof(line), "  %s  %dx%d  @%ds",
                         captures[i].filename, captures[i].width, captures[i].height,
                         captures[i].timestamp);
                neo::display::puts(line);
            }
        }

        // Status
        if (status_timer > 0) {
            neo::display::set_color(0, 2);
            neo::display::set_cursor(0, screen_h - 1);
            neo::display::puts(status_msg);
            for (int i = neo_strlen(status_msg); i < screen_w; i++) {
                neo::display::set_cursor(i, screen_h - 1);
                neo::display::putchar(' ');
            }
        }
    }

    void render_region_select() {
        neo::display::clear();
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " Region Select | Cursor: (%d,%d) | Press Enter at corners | Esc: Cancel ",
                 sel_cursor_x, sel_cursor_y);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Draw grid with selection
        neo::display::set_color(7, 0);
        for (int y = 1; y < screen_h - 1; y++) {
            for (int x = 0; x < screen_w; x++) {
                bool in_sel = selecting &&
                    x >= (sel_x1 < sel_cursor_x ? sel_x1 : sel_cursor_x) &&
                    x <= (sel_x1 > sel_cursor_x ? sel_x1 : sel_cursor_x) &&
                    y >= (sel_y1 < sel_cursor_y ? sel_y1 : sel_cursor_y) &&
                    y <= (sel_y1 > sel_cursor_y ? sel_y1 : sel_cursor_y);

                neo::display::set_cursor(x, y);
                if (x == sel_cursor_x && y == sel_cursor_y) {
                    neo::display::set_color(0, 7);
                    neo::display::putchar('+');
                } else if (in_sel) {
                    neo::display::set_color(0, 4);
                    neo::display::putchar('.');
                } else if (x % 10 == 0 || y % 5 == 0) {
                    neo::display::set_color(1, 0);
                    neo::display::putchar('.');
                } else {
                    neo::display::set_color(7, 0);
                    neo::display::putchar(' ');
                }
            }
        }

        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        if (selecting) {
            char info[80];
            ksprintf(info, sizeof(info), " Corner 1: (%d,%d) -> Move to corner 2 and press Enter ",
                     sel_x1, sel_y1);
            neo::display::puts(info);
        } else {
            neo::display::puts(" Arrows: Move | Enter: Set corner 1 | Esc: Cancel ");
        }
        for (int i = 50; i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 1);
            neo::display::putchar(' ');
        }
    }

    void render_gallery() {
        neo::display::clear();
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoCapture Gallery | %d captures | Arrows:Nav Enter:View Esc:Back D:Delete ",
                 num_captures);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        if (num_captures == 0) {
            neo::display::set_color(7, 0);
            neo::display::set_cursor(screen_w / 2 - 8, screen_h / 2);
            neo::display::puts("No captures yet!");
            return;
        }

        // List view
        neo::display::set_color(0, 6);
        neo::display::set_cursor(0, 2);
        neo::display::puts(" # | Filename             | Size      | Time     ");
        for (int i = 48; i < screen_w; i++) {
            neo::display::set_cursor(i, 2);
            neo::display::putchar(' ');
        }

        int visible = screen_h - 5;
        if (gallery_selected < gallery_scroll) gallery_scroll = gallery_selected;
        if (gallery_selected >= gallery_scroll + visible) gallery_scroll = gallery_selected - visible + 1;

        for (int i = 0; i < visible && gallery_scroll + i < num_captures; i++) {
            int idx = gallery_scroll + i;
            int sy = 3 + i;
            CaptureInfo& cap = captures[idx];

            if (idx == gallery_selected) neo::display::set_color(0, 7);
            else neo::display::set_color(7, 0);

            neo::display::set_cursor(0, sy);
            char line[80];
            ksprintf(line, sizeof(line), "%2d | %-20s | %3dx%-3d   | %ds",
                     idx + 1, cap.filename, cap.width, cap.height, cap.timestamp);
            neo::display::puts(line);
            for (int j = neo_strlen(line); j < screen_w; j++) {
                neo::display::set_cursor(j, sy);
                neo::display::putchar(' ');
            }
        }
    }

    void render_preview() {
        neo::display::clear();
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        CaptureInfo& cap = captures[gallery_selected];
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " Preview: %s (%dx%d) | Esc: Back ",
                 cap.filename, cap.width, cap.height);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Display preview content
        for (int y = 0; y < preview_buf.height && y + 2 < screen_h; y++) {
            for (int x = 0; x < preview_buf.width && x < screen_w; x++) {
                CaptureCell& c = preview_buf.cells[y][x];
                neo::display::set_color(c.fg, c.bg);
                neo::display::set_cursor(x, y + 2);
                neo::display::putchar(c.ch);
            }
        }
    }

    void do_timed_capture() {
        for (int i = countdown_secs; i > 0; i--) {
            neo::display::clear();
            neo::display::set_color(7, 0);
            int cx = screen_w / 2 - 5;
            int cy = screen_h / 2;
            neo::display::set_cursor(cx, cy - 2);
            neo::display::set_fg(3);
            neo::display::puts("Capturing in...");
            neo::display::set_cursor(cx + 4, cy);
            neo::display::set_fg(7);
            neo::display::set_bold(true);
            char num[4];
            ksprintf(num, sizeof(num), "%d", i);
            neo::display::puts(num);
            neo::display::set_bold(false);
            neo::timer::delay_ms(1000);
        }
        capture_full_screen();
    }

    void delete_capture(int idx) {
        if (idx < 0 || idx >= num_captures) return;
        // Shift remaining captures
        for (int i = idx; i < num_captures - 1; i++) {
            captures[i] = captures[i + 1];
        }
        num_captures--;
        if (gallery_selected >= num_captures && gallery_selected > 0) gallery_selected--;
        set_status("Capture deleted");
    }

    void handle_menu_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (ch == '1') { capture_full_screen(); }
        else if (ch == '2') { mode = MODE_REGION_SELECT; selecting = false; sel_cursor_x = 0; sel_cursor_y = 1; }
        else if (ch == '3') { do_timed_capture(); }
        else if (ch == '4') { mode = MODE_GALLERY; gallery_selected = 0; gallery_scroll = 0; }
        else if (ch == '5') { countdown_secs = (countdown_secs % 30) + 1; }
        else if (ch == 'q' || ch == 'Q' || sc == 0x45) { running = false; }
    }

    void handle_region_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (sc == 0x45) { mode = MODE_MENU; return; } // Esc
        if (sc == 0x4C && sel_cursor_y > 1) sel_cursor_y--;
        else if (sc == 0x4D && sel_cursor_y < screen_h - 2) sel_cursor_y++;
        else if (sc == 0x4F && sel_cursor_x > 0) sel_cursor_x--;
        else if (sc == 0x50 && sel_cursor_x < screen_w - 1) sel_cursor_x++;
        else if (ch == '\n' || ch == '\r' || sc == 0x44) {
            if (!selecting) {
                sel_x1 = sel_cursor_x;
                sel_y1 = sel_cursor_y;
                selecting = true;
            } else {
                sel_x2 = sel_cursor_x;
                sel_y2 = sel_cursor_y;
                selecting = false;
                capture_region(sel_x1, sel_y1, sel_x2, sel_y2);
                mode = MODE_MENU;
            }
        }
    }

    void handle_gallery_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (sc == 0x45) { mode = MODE_MENU; return; }
        if (sc == 0x4C && gallery_selected > 0) gallery_selected--;
        else if (sc == 0x4D && gallery_selected < num_captures - 1) gallery_selected++;
        else if ((ch == '\n' || ch == '\r' || sc == 0x44) && num_captures > 0) {
            load_capture_preview(gallery_selected);
            mode = MODE_PREVIEW;
        }
        else if ((ch == 'd' || ch == 'D') && num_captures > 0) {
            delete_capture(gallery_selected);
        }
    }

    void handle_preview_key(unsigned char sc) {
        if (sc == 0x45 || sc == 0x44) { mode = MODE_GALLERY; }
    }

    void run() {
        init();

        while (running) {
            switch (mode) {
                case MODE_MENU: render_menu(); break;
                case MODE_REGION_SELECT: render_region_select(); break;
                case MODE_GALLERY: render_gallery(); break;
                case MODE_PREVIEW: render_preview(); break;
                default: render_menu(); break;
            }

            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            while (!neo::keyboard::key_available()) neo::timer::delay_ms(10);
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc & 0x80) continue;

            switch (mode) {
                case MODE_MENU: handle_menu_key(sc); break;
                case MODE_REGION_SELECT: handle_region_key(sc); break;
                case MODE_GALLERY: handle_gallery_key(sc); break;
                case MODE_PREVIEW: handle_preview_key(sc); break;
                default: break;
            }
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoCapture: Goodbye.\n");
    }
};

} // namespace neocapture

extern "C" void app_main(int argc, char** argv) {
    neocapture::CaptureApp app;
    app.run();
}
