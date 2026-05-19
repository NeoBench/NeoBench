#include "../include/neobench.h"
#include "../lib/string.h"

// NeoAnim - Frame-based text animation tool
// Create/edit frames, playback, onion skin, timeline, save/load

namespace neoanim {

static const int FRAME_W = 60;
static const int FRAME_H = 20;
static const int MAX_FRAMES = 64;
static const int TIMELINE_H = 3;

struct Cell {
    char ch;
    unsigned char fg;
    unsigned char bg;
};

struct Frame {
    Cell cells[FRAME_H][FRAME_W];
    int duration_ms; // display duration
    bool used;

    void clear() {
        for (int y = 0; y < FRAME_H; y++)
            for (int x = 0; x < FRAME_W; x++) {
                cells[y][x].ch = ' ';
                cells[y][x].fg = 7;
                cells[y][x].bg = 0;
            }
        duration_ms = 200;
        used = true;
    }

    void copy_from(const Frame& other) {
        for (int y = 0; y < FRAME_H; y++)
            for (int x = 0; x < FRAME_W; x++)
                cells[y][x] = other.cells[y][x];
        duration_ms = other.duration_ms;
        used = other.used;
    }
};

struct AnimApp {
    Frame* frames;
    int num_frames;
    int cur_frame;
    int cursor_x, cursor_y;
    int screen_w, screen_h;
    bool running;
    bool playing;
    bool onion_skin;
    bool editing;
    unsigned char fg_color;
    unsigned char bg_color;
    char brush_char;
    char status_msg[64];
    int status_timer;
    char filename[128];

    // Clipboard
    Frame clipboard;
    bool clipboard_valid;

    int timeline_scroll;

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        frames = (Frame*)neo::mem::alloc(sizeof(Frame) * MAX_FRAMES);
        if (!frames) {
            kprintf("NeoAnim: Out of memory!\n");
            running = false;
            return;
        }
        num_frames = 1;
        frames[0].clear();
        cur_frame = 0;
        cursor_x = 0;
        cursor_y = 0;
        running = true;
        playing = false;
        onion_skin = false;
        editing = true;
        fg_color = 7;
        bg_color = 0;
        brush_char = '#';
        status_msg[0] = '\0';
        status_timer = 0;
        neo_strcpy(filename, "anim.nan");
        clipboard_valid = false;
        timeline_scroll = 0;
    }

    void destroy() {
        if (frames) neo::mem::free(frames);
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    Frame& current() { return frames[cur_frame]; }

    void add_frame() {
        if (num_frames >= MAX_FRAMES) { set_status("Max frames reached!"); return; }
        // Insert after current
        for (int i = num_frames; i > cur_frame + 1; i--) {
            frames[i].copy_from(frames[i - 1]);
        }
        num_frames++;
        cur_frame++;
        frames[cur_frame].clear();
        set_status("Frame added");
    }

    void delete_frame() {
        if (num_frames <= 1) { set_status("Can't delete last frame"); return; }
        for (int i = cur_frame; i < num_frames - 1; i++) {
            frames[i].copy_from(frames[i + 1]);
        }
        num_frames--;
        if (cur_frame >= num_frames) cur_frame = num_frames - 1;
        set_status("Frame deleted");
    }

    void copy_frame() {
        clipboard.copy_from(current());
        clipboard_valid = true;
        set_status("Frame copied");
    }

    void paste_frame() {
        if (!clipboard_valid) { set_status("Nothing to paste"); return; }
        current().copy_from(clipboard);
        set_status("Frame pasted");
    }

    void dup_frame() {
        if (num_frames >= MAX_FRAMES) { set_status("Max frames reached!"); return; }
        for (int i = num_frames; i > cur_frame + 1; i--) {
            frames[i].copy_from(frames[i - 1]);
        }
        num_frames++;
        frames[cur_frame + 1].copy_from(frames[cur_frame]);
        cur_frame++;
        set_status("Frame duplicated");
    }

    void render_frame_area() {
        int ox = 2; // offset x for frame display
        int oy = 2;

        // Frame border
        neo::display::set_color(6, 0);
        neo::display::set_cursor(ox - 1, oy - 1);
        neo::display::putchar('+');
        for (int x = 0; x < FRAME_W; x++) neo::display::putchar('-');
        neo::display::putchar('+');

        for (int y = 0; y < FRAME_H; y++) {
            int sy = oy + y;
            if (sy >= screen_h - TIMELINE_H - 3) break;

            neo::display::set_color(6, 0);
            neo::display::set_cursor(ox - 1, sy);
            neo::display::putchar('|');

            for (int x = 0; x < FRAME_W; x++) {
                Cell c = current().cells[y][x];

                // Onion skin: show previous frame dimmed
                if (onion_skin && cur_frame > 0 && c.ch == ' ') {
                    Cell prev = frames[cur_frame - 1].cells[y][x];
                    if (prev.ch != ' ') {
                        neo::display::set_color(1, 0); // dim
                        if (editing && x == cursor_x && y == cursor_y) {
                            neo::display::set_color(0, 1);
                        }
                        neo::display::set_cursor(ox + x, sy);
                        neo::display::putchar(prev.ch);
                        continue;
                    }
                }

                if (editing && x == cursor_x && y == cursor_y) {
                    neo::display::set_color(0, 7);
                } else {
                    neo::display::set_color(c.fg, c.bg);
                }
                neo::display::set_cursor(ox + x, sy);
                neo::display::putchar(c.ch);
            }

            neo::display::set_color(6, 0);
            neo::display::set_cursor(ox + FRAME_W, sy);
            neo::display::putchar('|');
        }

        neo::display::set_color(6, 0);
        int bottom = oy + FRAME_H;
        if (bottom < screen_h - TIMELINE_H - 2) {
            neo::display::set_cursor(ox - 1, bottom);
            neo::display::putchar('+');
            for (int x = 0; x < FRAME_W; x++) neo::display::putchar('-');
            neo::display::putchar('+');
        }
    }

    void render_info_panel() {
        int px = FRAME_W + 5;
        if (px + 14 >= screen_w) return;

        neo::display::set_color(7, 0);
        neo::display::set_cursor(px, 2);
        char buf[32];
        ksprintf(buf, sizeof(buf), "Frame: %d/%d", cur_frame + 1, num_frames);
        neo::display::puts(buf);

        neo::display::set_cursor(px, 3);
        ksprintf(buf, sizeof(buf), "Dur: %dms", current().duration_ms);
        neo::display::puts(buf);

        neo::display::set_cursor(px, 4);
        ksprintf(buf, sizeof(buf), "Pos: %d,%d", cursor_x, cursor_y);
        neo::display::puts(buf);

        neo::display::set_cursor(px, 5);
        ksprintf(buf, sizeof(buf), "FG:%d BG:%d", fg_color, bg_color);
        neo::display::puts(buf);

        neo::display::set_cursor(px, 6);
        ksprintf(buf, sizeof(buf), "Brush: '%c'", brush_char);
        neo::display::puts(buf);

        neo::display::set_cursor(px, 7);
        neo::display::puts(onion_skin ? "Onion: ON " : "Onion: OFF");

        // Controls
        neo::display::set_cursor(px, 9);
        neo::display::set_fg(6);
        neo::display::puts("Controls:");
        neo::display::set_fg(7);
        neo::display::set_cursor(px, 10); neo::display::puts("Space: Draw");
        neo::display::set_cursor(px, 11); neo::display::puts("A: Add frame");
        neo::display::set_cursor(px, 12); neo::display::puts("D: Dup frame");
        neo::display::set_cursor(px, 13); neo::display::puts("X: Del frame");
        neo::display::set_cursor(px, 14); neo::display::puts("[/]: Prev/Next");
        neo::display::set_cursor(px, 15); neo::display::puts("P: Play/Stop");
        neo::display::set_cursor(px, 16); neo::display::puts("O: Onion skin");
        neo::display::set_cursor(px, 17); neo::display::puts("+/-: Duration");
        neo::display::set_cursor(px, 18); neo::display::puts("C/V: Copy/Paste");
        neo::display::set_cursor(px, 19); neo::display::puts("F/B: FG/BG col");
        neo::display::set_cursor(px, 20); neo::display::puts("S: Save  L:Load");
    }

    void render_timeline() {
        int ty = screen_h - TIMELINE_H - 1;
        neo::display::set_color(0, 6);
        neo::display::set_cursor(0, ty);
        neo::display::puts(" Timeline ");
        for (int i = 10; i < screen_w; i++) {
            neo::display::set_cursor(i, ty);
            neo::display::putchar(' ');
        }

        int visible = (screen_w - 2) / 5;
        if (cur_frame < timeline_scroll) timeline_scroll = cur_frame;
        if (cur_frame >= timeline_scroll + visible) timeline_scroll = cur_frame - visible + 1;

        neo::display::set_color(7, 0);
        for (int i = 0; i < visible && timeline_scroll + i < num_frames; i++) {
            int fi = timeline_scroll + i;
            int tx = 1 + i * 5;
            if (tx + 4 >= screen_w) break;

            if (fi == cur_frame) neo::display::set_color(0, 7);
            else neo::display::set_color(7, 0);

            neo::display::set_cursor(tx, ty + 1);
            char fbuf[6];
            ksprintf(fbuf, sizeof(fbuf), "[%02d]", fi + 1);
            neo::display::puts(fbuf);

            neo::display::set_cursor(tx, ty + 2);
            // Show tiny preview - just check if frame has content
            bool has_content = false;
            for (int cy = 0; cy < FRAME_H && !has_content; cy++)
                for (int cx = 0; cx < FRAME_W && !has_content; cx++)
                    if (frames[fi].cells[cy][cx].ch != ' ') has_content = true;
            neo::display::puts(has_content ? " ** " : " .. ");
        }
    }

    void render() {
        neo::display::clear();

        // Header
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoAnim | %s | %s ", filename, playing ? "PLAYING" : "EDITING");
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        render_frame_area();
        if (editing && !playing) render_info_panel();
        render_timeline();

        // Status
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        if (status_timer > 0) {
            char sbuf[80];
            ksprintf(sbuf, sizeof(sbuf), " %s", status_msg);
            neo::display::puts(sbuf);
            for (int i = neo_strlen(sbuf); i < screen_w; i++) {
                neo::display::set_cursor(i, screen_h - 1);
                neo::display::putchar(' ');
            }
        } else {
            neo::display::puts(" Q:Quit | Arrows:Move | Space:Draw");
            for (int i = 36; i < screen_w; i++) {
                neo::display::set_cursor(i, screen_h - 1);
                neo::display::putchar(' ');
            }
        }
    }

    void play_animation() {
        playing = true;
        int start_frame = 0;

        while (playing) {
            for (int f = start_frame; f < num_frames && playing; f++) {
                cur_frame = f;
                render();
                // Wait for duration or key
                int waited = 0;
                while (waited < frames[f].duration_ms && playing) {
                    neo::timer::delay_ms(10);
                    waited += 10;
                    if (neo::keyboard::key_available()) {
                        unsigned char sc = neo::keyboard::read_scancode();
                        if (!(sc & 0x80)) {
                            char ch = neo::keyboard::translate(sc, false);
                            if (ch == 'p' || ch == 'P' || sc == 0x45) {
                                playing = false;
                            }
                        }
                    }
                }
            }
            // Loop
            if (playing) start_frame = 0;
        }
        editing = true;
        set_status("Playback stopped");
    }

    void save_animation() {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
            // Header: magic + frame count
            char magic[4] = {'N', 'A', 'N', '1'};
            neo::filesystem::write(fh, magic, 4);
            neo::filesystem::write(fh, (char*)&num_frames, 4);
            neo::filesystem::write(fh, (char*)&FRAME_W, 4);
            neo::filesystem::write(fh, (char*)&FRAME_H, 4);

            for (int f = 0; f < num_frames; f++) {
                neo::filesystem::write(fh, (char*)&frames[f].duration_ms, 4);
                for (int y = 0; y < FRAME_H; y++) {
                    for (int x = 0; x < FRAME_W; x++) {
                        char data[3] = {
                            frames[f].cells[y][x].ch,
                            (char)frames[f].cells[y][x].fg,
                            (char)frames[f].cells[y][x].bg
                        };
                        neo::filesystem::write(fh, data, 3);
                    }
                }
            }
            neo::filesystem::close(fh);
            set_status("Animation saved!");
        } else {
            set_status("Save failed!");
        }
    }

    void load_animation() {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_READ) == 0) {
            char magic[4];
            neo::filesystem::read(fh, magic, 4);
            if (neo_strncmp(magic, "NAN1", 4) != 0) {
                neo::filesystem::close(fh);
                set_status("Invalid file!");
                return;
            }
            int fw, fh_val;
            neo::filesystem::read(fh, (char*)&num_frames, 4);
            neo::filesystem::read(fh, (char*)&fw, 4);
            neo::filesystem::read(fh, (char*)&fh_val, 4);
            if (num_frames > MAX_FRAMES) num_frames = MAX_FRAMES;

            for (int f = 0; f < num_frames; f++) {
                frames[f].used = true;
                neo::filesystem::read(fh, (char*)&frames[f].duration_ms, 4);
                for (int y = 0; y < FRAME_H && y < fh_val; y++) {
                    for (int x = 0; x < FRAME_W && x < fw; x++) {
                        char data[3];
                        neo::filesystem::read(fh, data, 3);
                        frames[f].cells[y][x].ch = data[0];
                        frames[f].cells[y][x].fg = (unsigned char)data[1];
                        frames[f].cells[y][x].bg = (unsigned char)data[2];
                    }
                }
            }
            neo::filesystem::close(fh);
            cur_frame = 0;
            set_status("Animation loaded!");
        } else {
            set_status("Load failed!");
        }
    }

    void export_script() {
        char spath[128];
        ksprintf(spath, sizeof(spath), "%s.sh", filename);
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, spath, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
            char line[INODE_SIZE];
            neo_strcpy(line, "#!/bin/sh\n# NeoAnim export\n");
            neo::filesystem::write(fh, line, neo_strlen(line));
            for (int f = 0; f < num_frames; f++) {
                ksprintf(line, sizeof(line), "echo \"--- Frame %d ---\"\n", f + 1);
                neo::filesystem::write(fh, line, neo_strlen(line));
                for (int y = 0; y < FRAME_H; y++) {
                    neo_strcpy(line, "echo \"");
                    int pos = 6;
                    for (int x = 0; x < FRAME_W && pos < 240; x++) {
                        line[pos++] = frames[f].cells[y][x].ch;
                    }
                    line[pos++] = '"';
                    line[pos++] = '\n';
                    line[pos] = '\0';
                    neo::filesystem::write(fh, line, pos);
                }
                ksprintf(line, sizeof(line), "sleep %d\n", frames[f].duration_ms / 1000);
                if (frames[f].duration_ms < 1000) neo_strcpy(line, "sleep 1\n");
                neo::filesystem::write(fh, line, neo_strlen(line));
            }
            neo::filesystem::close(fh);
            set_status("Script exported!");
        }
    }

    void handle_key(unsigned char sc) {
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        // Arrow keys
        if (sc == 0x4C && cursor_y > 0) cursor_y--;
        else if (sc == 0x4D && cursor_y < FRAME_H - 1) cursor_y++;
        else if (sc == 0x4F && cursor_x > 0) cursor_x--;
        else if (sc == 0x50 && cursor_x < FRAME_W - 1) cursor_x++;

        // Draw
        else if (ch == ' ') {
            current().cells[cursor_y][cursor_x].ch = brush_char;
            current().cells[cursor_y][cursor_x].fg = fg_color;
            current().cells[cursor_y][cursor_x].bg = bg_color;
        }
        // Erase
        else if (sc == 0x41 || sc == 0x46) { // Backspace or Delete
            current().cells[cursor_y][cursor_x].ch = ' ';
            current().cells[cursor_y][cursor_x].fg = 7;
            current().cells[cursor_y][cursor_x].bg = 0;
        }
        // Frame navigation
        else if (ch == '[' || ch == '{') { if (cur_frame > 0) cur_frame--; }
        else if (ch == ']' || ch == '}') { if (cur_frame < num_frames - 1) cur_frame++; }
        // Frame operations
        else if (ch == 'a' || ch == 'A') add_frame();
        else if (ch == 'd' || ch == 'D') dup_frame();
        else if (ch == 'x' || ch == 'X') delete_frame();
        // Copy/Paste
        else if (ch == 'c' || ch == 'C') copy_frame();
        else if (ch == 'v' || ch == 'V') paste_frame();
        // Play
        else if (ch == 'p' || ch == 'P') {
            editing = false;
            play_animation();
        }
        // Onion skin
        else if (ch == 'o' || ch == 'O') {
            onion_skin = !onion_skin;
            set_status(onion_skin ? "Onion skin ON" : "Onion skin OFF");
        }
        // Duration
        else if (ch == '+' || ch == '=') { current().duration_ms += 50; set_status("Duration +50ms"); }
        else if (ch == '-') { if (current().duration_ms > 50) current().duration_ms -= 50; set_status("Duration -50ms"); }
        // Colors
        else if (ch == 'f' || ch == 'F') fg_color = (fg_color + 1) % 16;
        else if (ch == 'b' || ch == 'B') bg_color = (bg_color + 1) % 16;
        // Brush
        else if (ch == 'r' || ch == 'R') {
            // Cycle brush chars
            const char brs[] = "#@*+.O=~%&$^!?<>";
            for (int i = 0; brs[i]; i++) {
                if (brush_char == brs[i]) {
                    brush_char = brs[i + 1] ? brs[i + 1] : brs[0];
                    return;
                }
            }
            brush_char = '#';
        }
        // Save/Load
        else if (ch == 's' || ch == 'S') save_animation();
        else if (ch == 'l' || ch == 'L') load_animation();
        // Export
        else if (ch == 'e' || ch == 'E') export_script();
        // Clear frame
        else if (ch == 'k' || ch == 'K') { current().clear(); set_status("Frame cleared"); }
        // Quit
        else if (ch == 'q' || ch == 'Q' || sc == 0x45) running = false;
        // Type character directly
        else if (ch >= 33 && ch < 127 && ch != 'q' && ch != 'Q') {
            current().cells[cursor_y][cursor_x].ch = ch;
            current().cells[cursor_y][cursor_x].fg = fg_color;
            current().cells[cursor_y][cursor_x].bg = bg_color;
            if (cursor_x < FRAME_W - 1) cursor_x++;
        }
    }

    void run() {
        init();
        if (!running) return;

        while (running) {
            render();
            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            while (!neo::keyboard::key_available()) neo::timer::delay_ms(10);
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc & 0x80) continue;
            handle_key(sc);
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoAnim: Goodbye.\n");
        destroy();
    }
};

} // namespace neoanim

extern "C" void app_main(int argc, char** argv) {
    neoanim::AnimApp app;
    app.run();
}
