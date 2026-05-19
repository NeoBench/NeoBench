#include "../include/neobench.h"
#include "../lib/string.h"

// NeoPaint - Bitmap Graphics Editor (text-mode)
// Canvas, drawing tools, color palette, undo, save/load

namespace neopaint {

static const int CANVAS_W = 76;
static const int CANVAS_H = 40;
static const int MAX_UNDO = 20;
static const int PALETTE_COLORS = 16;

enum Tool {
    TOOL_PENCIL = 0,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_FILLED_RECT,
    TOOL_CIRCLE,
    TOOL_FLOOD_FILL,
    TOOL_ERASER,
    TOOL_COUNT
};

static const char* tool_names[] = {
    "Pencil", "Line", "Rect", "FillRect", "Circle", "Fill", "Eraser"
};

struct Cell {
    char ch;
    unsigned char fg;
    unsigned char bg;
};

struct Canvas {
    Cell cells[CANVAS_H][CANVAS_W];

    void clear() {
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W; x++) {
                cells[y][x].ch = ' ';
                cells[y][x].fg = 7;
                cells[y][x].bg = 0;
            }
    }

    void set(int x, int y, char ch, unsigned char fg, unsigned char bg) {
        if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H) {
            cells[y][x].ch = ch;
            cells[y][x].fg = fg;
            cells[y][x].bg = bg;
        }
    }

    Cell get(int x, int y) const {
        if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
            return cells[y][x];
        Cell empty = {' ', 7, 0};
        return empty;
    }

    void copy_from(const Canvas& other) {
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W; x++)
                cells[y][x] = other.cells[y][x];
    }
};

struct UndoStack {
    Canvas* snapshots[MAX_UNDO];
    int count;
    int head;

    void init() {
        count = 0;
        head = 0;
        for (int i = 0; i < MAX_UNDO; i++) {
            snapshots[i] = (Canvas*)neo::mem::alloc(sizeof(Canvas));
        }
    }

    void destroy() {
        for (int i = 0; i < MAX_UNDO; i++) {
            if (snapshots[i]) neo::mem::free(snapshots[i]);
        }
    }

    void push(const Canvas& canvas) {
        snapshots[head]->copy_from(canvas);
        head = (head + 1) % MAX_UNDO;
        if (count < MAX_UNDO) count++;
    }

    bool pop(Canvas& canvas) {
        if (count == 0) return false;
        head = (head - 1 + MAX_UNDO) % MAX_UNDO;
        count--;
        canvas.copy_from(*snapshots[head]);
        return true;
    }
};

struct PaintApp {
    Canvas canvas;
    UndoStack undo;
    Tool current_tool;
    unsigned char fg_color;
    unsigned char bg_color;
    char brush_char;
    int cursor_x, cursor_y;
    int start_x, start_y; // for line/rect
    bool drawing;
    bool running;
    int screen_w, screen_h;
    char filename[128];
    char status_msg[64];
    int status_timer;

    // Brush characters
    static const int NUM_BRUSHES = 12;
    char brushes[NUM_BRUSHES];

    int abs_val(int x) { return x < 0 ? -x : x; }

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        canvas.clear();
        undo.init();
        current_tool = TOOL_PENCIL;
        fg_color = 7;
        bg_color = 0;
        brush_char = '#';
        cursor_x = CANVAS_W / 2;
        cursor_y = CANVAS_H / 2;
        start_x = start_y = 0;
        drawing = false;
        running = true;
        neo_strcpy(filename, "canvas.npc");
        status_msg[0] = '\0';
        status_timer = 0;

        brushes[0] = '#';  brushes[1] = '@';  brushes[2] = '*';
        brushes[3] = '+';  brushes[4] = '.';  brushes[5] = 'O';
        brushes[6] = '=';  brushes[7] = '~';  brushes[8] = '%';
        brushes[9] = '&';  brushes[10] = '$'; brushes[11] = '^';
    }

    void destroy() {
        undo.destroy();
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    void save_undo() {
        undo.push(canvas);
    }

    void do_undo() {
        if (undo.pop(canvas)) set_status("Undo");
        else set_status("Nothing to undo");
    }

    void plot(int x, int y) {
        canvas.set(x, y, brush_char, fg_color, bg_color);
    }

    void draw_line(int x0, int y0, int x1, int y1) {
        int dx = abs_val(x1 - x0);
        int dy = -abs_val(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void draw_rect(int x0, int y0, int x1, int y1, bool filled) {
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

        if (filled) {
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                    plot(x, y);
        } else {
            for (int x = x0; x <= x1; x++) { plot(x, y0); plot(x, y1); }
            for (int y = y0; y <= y1; y++) { plot(x0, y); plot(x1, y); }
        }
    }

    void draw_circle(int cx, int cy, int r) {
        int x = r, y = 0;
        int err = 1 - r;

        while (x >= y) {
            plot(cx + x, cy + y); plot(cx - x, cy + y);
            plot(cx + x, cy - y); plot(cx - x, cy - y);
            plot(cx + y, cy + x); plot(cx - y, cy + x);
            plot(cx + y, cy - x); plot(cx - y, cy - x);
            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }

    void flood_fill(int x, int y, char target_ch, unsigned char target_fg) {
        if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;
        Cell c = canvas.get(x, y);
        if (c.ch != target_ch || c.fg != target_fg) return;
        if (c.ch == brush_char && c.fg == fg_color && c.bg == bg_color) return;

        // Stack-based flood fill to avoid deep recursion
        struct Pt { short x, y; };
        Pt* stack = (Pt*)neo::mem::alloc(sizeof(Pt) * CANVAS_W * CANVAS_H);
        if (!stack) return;
        int sp = 0;
        stack[sp++] = {(short)x, (short)y};

        while (sp > 0) {
            Pt p = stack[--sp];
            if (p.x < 0 || p.x >= CANVAS_W || p.y < 0 || p.y >= CANVAS_H) continue;
            Cell cc = canvas.get(p.x, p.y);
            if (cc.ch != target_ch || cc.fg != target_fg) continue;

            canvas.set(p.x, p.y, brush_char, fg_color, bg_color);

            if (sp + 4 < CANVAS_W * CANVAS_H) {
                stack[sp++] = {(short)(p.x + 1), p.y};
                stack[sp++] = {(short)(p.x - 1), p.y};
                stack[sp++] = {p.x, (short)(p.y + 1)};
                stack[sp++] = {p.x, (short)(p.y - 1)};
            }
        }
        neo::mem::free(stack);
    }

    void render() {
        neo::display::clear();
        int canvas_offset_x = 2;
        int canvas_offset_y = 1;

        // Title
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char title[80];
        ksprintf(title, sizeof(title), " NeoPaint | Tool: %s | Brush: '%c' | FG:%d BG:%d | (%d,%d) ",
                 tool_names[current_tool], brush_char, fg_color, bg_color, cursor_x, cursor_y);
        neo::display::puts(title);
        for (int i = neo_strlen(title); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Canvas border
        neo::display::set_color(7, 0);
        // Draw top/bottom borders
        neo::display::set_cursor(canvas_offset_x - 1, canvas_offset_y - 0);
        neo::display::putchar('+');
        for (int x = 0; x < CANVAS_W; x++) neo::display::putchar('-');
        neo::display::putchar('+');

        int bottom_y = canvas_offset_y + CANVAS_H + 1;
        if (bottom_y < screen_h - 2) {
            neo::display::set_cursor(canvas_offset_x - 1, bottom_y - 1);
            neo::display::putchar('+');
            for (int x = 0; x < CANVAS_W; x++) neo::display::putchar('-');
            neo::display::putchar('+');
        }

        // Canvas content
        int max_y = CANVAS_H;
        if (canvas_offset_y + max_y > screen_h - 3) max_y = screen_h - 3 - canvas_offset_y;

        for (int y = 0; y < max_y; y++) {
            int sy = canvas_offset_y + y + 1;
            if (sy >= screen_h - 2) break;
            neo::display::set_color(7, 0);
            neo::display::set_cursor(canvas_offset_x - 1, sy - 1);
            neo::display::putchar('|');

            for (int x = 0; x < CANVAS_W; x++) {
                Cell c = canvas.get(x, y);
                if (x == cursor_x && y == cursor_y) {
                    // Cursor highlight
                    neo::display::set_color(0, 7);
                } else {
                    neo::display::set_color(c.fg, c.bg);
                }
                neo::display::putchar(c.ch);
            }
            neo::display::set_color(7, 0);
            neo::display::putchar('|');
        }

        // Color palette at right side
        int pal_x = canvas_offset_x + CANVAS_W + 3;
        if (pal_x + 5 < screen_w) {
            neo::display::set_color(7, 0);
            neo::display::set_cursor(pal_x, 1);
            neo::display::puts("Color:");
            for (int i = 0; i < PALETTE_COLORS; i++) {
                int py = 2 + i;
                if (py >= screen_h - 2) break;
                neo::display::set_cursor(pal_x, py);
                if (i == (int)fg_color) neo::display::set_color(0, 7);
                else neo::display::set_color(7, 0);
                char num[4];
                ksprintf(num, sizeof(num), "%2d", i);
                neo::display::puts(num);
                neo::display::set_color(i, 0);
                neo::display::puts(" ##");
            }
        }

        // Status / help bar
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 2);
        char help[128];
        if (status_timer > 0) {
            ksprintf(help, sizeof(help), " %s", status_msg);
        } else {
            neo_strcpy(help, " Arrows:Move Space:Draw T:Tool C:Color B:Brush U:Undo S:Save L:Load Q:Quit");
        }
        neo::display::puts(help);
        for (int i = neo_strlen(help); i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 2);
            neo::display::putchar(' ');
        }
    }

    void apply_tool() {
        save_undo();
        switch (current_tool) {
            case TOOL_PENCIL:
                plot(cursor_x, cursor_y);
                break;
            case TOOL_ERASER:
                canvas.set(cursor_x, cursor_y, ' ', 7, 0);
                break;
            case TOOL_FLOOD_FILL: {
                Cell target = canvas.get(cursor_x, cursor_y);
                flood_fill(cursor_x, cursor_y, target.ch, target.fg);
                break;
            }
            case TOOL_LINE:
                if (!drawing) {
                    start_x = cursor_x;
                    start_y = cursor_y;
                    drawing = true;
                    set_status("Line: move to end, press Space");
                } else {
                    draw_line(start_x, start_y, cursor_x, cursor_y);
                    drawing = false;
                }
                break;
            case TOOL_RECT:
                if (!drawing) {
                    start_x = cursor_x;
                    start_y = cursor_y;
                    drawing = true;
                    set_status("Rect: move to corner, press Space");
                } else {
                    draw_rect(start_x, start_y, cursor_x, cursor_y, false);
                    drawing = false;
                }
                break;
            case TOOL_FILLED_RECT:
                if (!drawing) {
                    start_x = cursor_x;
                    start_y = cursor_y;
                    drawing = true;
                    set_status("FillRect: move to corner, press Space");
                } else {
                    draw_rect(start_x, start_y, cursor_x, cursor_y, true);
                    drawing = false;
                }
                break;
            case TOOL_CIRCLE:
                if (!drawing) {
                    start_x = cursor_x;
                    start_y = cursor_y;
                    drawing = true;
                    set_status("Circle: move to set radius, press Space");
                } else {
                    int dx = cursor_x - start_x;
                    int dy = cursor_y - start_y;
                    int r = dx;
                    if (dy > r) r = dy;
                    if (r < 0) r = -r;
                    draw_circle(start_x, start_y, r);
                    drawing = false;
                }
                break;
            default:
                break;
        }
    }

    void save_canvas() {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
            // Header
            char hdr[16] = "NPC1";
            neo::filesystem::write(fh, hdr, 4);
            // Canvas data
            for (int y = 0; y < CANVAS_H; y++) {
                for (int x = 0; x < CANVAS_W; x++) {
                    Cell c = canvas.get(x, y);
                    char data[3] = {c.ch, (char)c.fg, (char)c.bg};
                    neo::filesystem::write(fh, data, 3);
                }
            }
            neo::filesystem::close(fh);
            set_status("Canvas saved!");
        } else {
            set_status("Save failed!");
        }
    }

    void load_canvas() {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, filename, neo::filesystem::MODE_READ) == 0) {
            char hdr[4];
            neo::filesystem::read(fh, hdr, 4);
            if (neo_strncmp(hdr, "NPC1", 4) == 0) {
                save_undo();
                for (int y = 0; y < CANVAS_H; y++) {
                    for (int x = 0; x < CANVAS_W; x++) {
                        char data[3];
                        neo::filesystem::read(fh, data, 3);
                        canvas.set(x, y, data[0], (unsigned char)data[1], (unsigned char)data[2]);
                    }
                }
                set_status("Canvas loaded!");
            } else {
                set_status("Invalid file format");
            }
            neo::filesystem::close(fh);
        } else {
            set_status("Load failed!");
        }
    }

    void run() {
        init();
        int brush_idx = 0;

        while (running) {
            render();
            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            while (!neo::keyboard::key_available()) neo::timer::delay_ms(10);
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc & 0x80) continue;

            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            // Arrow keys
            if (sc == 0x4C && cursor_y > 0) cursor_y--;
            else if (sc == 0x4D && cursor_y < CANVAS_H - 1) cursor_y++;
            else if (sc == 0x4F && cursor_x > 0) cursor_x--;
            else if (sc == 0x50 && cursor_x < CANVAS_W - 1) cursor_x++;
            else if (ch == ' ') apply_tool();
            else if (ch == 't' || ch == 'T') {
                current_tool = (Tool)(((int)current_tool + 1) % TOOL_COUNT);
                drawing = false;
                char msg[32];
                ksprintf(msg, sizeof(msg), "Tool: %s", tool_names[current_tool]);
                set_status(msg);
            }
            else if (ch == 'c' || ch == 'C') {
                fg_color = (fg_color + 1) % PALETTE_COLORS;
            }
            else if (ch == 'v' || ch == 'V') {
                bg_color = (bg_color + 1) % PALETTE_COLORS;
            }
            else if (ch == 'b' || ch == 'B') {
                brush_idx = (brush_idx + 1) % NUM_BRUSHES;
                brush_char = brushes[brush_idx];
            }
            else if (ch == 'u' || ch == 'U') do_undo();
            else if (ch == 's' || ch == 'S') save_canvas();
            else if (ch == 'l' || ch == 'L') load_canvas();
            else if (ch == 'x' || ch == 'X') { save_undo(); canvas.clear(); set_status("Canvas cleared"); }
            else if (ch == 'q' || ch == 'Q') running = false;
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoPaint: Goodbye.\n");
        destroy();
    }
};

} // namespace neopaint

extern "C" void app_main(int argc, char** argv) {
    neopaint::PaintApp app;
    app.run();
}
