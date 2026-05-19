#include "../include/neobench.h"
#include "../lib/string.h"

// NeoMines - Minesweeper clone for NeoBench

namespace {

enum Difficulty { EASY = 0, MEDIUM, HARD };
enum CellState { HIDDEN = 0, REVEALED, FLAGGED };

struct Cell {
    bool mine;
    CellState state;
    int adjacent;
};

struct HighScore {
    char name[16];
    int time_secs;
    bool valid;
};

const int MAX_W = 30;
const int MAX_H = 16;

struct Game {
    Cell grid[MAX_H][MAX_W];
    int width, height, total_mines;
    int cursor_x, cursor_y;
    int flags_placed;
    int cells_revealed;
    bool game_over, won;
    bool first_click;
    unsigned int start_ticks;
    int elapsed_secs;
    Difficulty difficulty;
    HighScore scores[3]; // one per difficulty
    unsigned int rng_state;
};

Game g;

unsigned int rng_next() {
    g.rng_state = g.rng_state * 1103515245 + 12345;
    return (g.rng_state >> 16) & 0x7FFF;
}

void init_scores() {
    for (int i = 0; i < 3; i++) {
        g.scores[i].valid = false;
        g.scores[i].time_secs = 9999;
        neo_strcpy(g.scores[i].name, "---");
    }
}

void init_grid() {
    for (int y = 0; y < g.height; y++) {
        for (int x = 0; x < g.width; x++) {
            g.grid[y][x].mine = false;
            g.grid[y][x].state = HIDDEN;
            g.grid[y][x].adjacent = 0;
        }
    }
    g.flags_placed = 0;
    g.cells_revealed = 0;
    g.game_over = false;
    g.won = false;
    g.first_click = true;
    g.elapsed_secs = 0;
}

void place_mines(int safe_x, int safe_y) {
    int placed = 0;
    while (placed < g.total_mines) {
        int x = rng_next() % g.width;
        int y = rng_next() % g.height;
        // Keep safe zone around first click
        int dx = x - safe_x;
        int dy = y - safe_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= 1 && dy <= 1) continue;
        if (g.grid[y][x].mine) continue;
        g.grid[y][x].mine = true;
        placed++;
    }
    // Calculate adjacency
    for (int y = 0; y < g.height; y++) {
        for (int x = 0; x < g.width; x++) {
            if (g.grid[y][x].mine) continue;
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < g.width && ny >= 0 && ny < g.height) {
                        if (g.grid[ny][nx].mine) count++;
                    }
                }
            }
            g.grid[y][x].adjacent = count;
        }
    }
}

void set_difficulty(Difficulty d) {
    g.difficulty = d;
    switch (d) {
        case EASY:   g.width = 9;  g.height = 9;  g.total_mines = 10; break;
        case MEDIUM: g.width = 16; g.height = 16; g.total_mines = 40; break;
        case HARD:   g.width = 30; g.height = 16; g.total_mines = 99; break;
    }
}

void flood_fill(int x, int y) {
    if (x < 0 || x >= g.width || y < 0 || y >= g.height) return;
    if (g.grid[y][x].state != HIDDEN) return;
    if (g.grid[y][x].mine) return;

    g.grid[y][x].state = REVEALED;
    g.cells_revealed++;

    if (g.grid[y][x].adjacent == 0) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                flood_fill(x + dx, y + dy);
            }
        }
    }
}

void check_win() {
    int safe_cells = g.width * g.height - g.total_mines;
    if (g.cells_revealed == safe_cells) {
        g.won = true;
        g.game_over = true;
        // Auto-flag remaining mines
        for (int y = 0; y < g.height; y++)
            for (int x = 0; x < g.width; x++)
                if (g.grid[y][x].mine && g.grid[y][x].state == HIDDEN)
                    g.grid[y][x].state = FLAGGED;
    }
}

void reveal_cell(int x, int y) {
    if (g.game_over) return;
    if (g.grid[y][x].state == FLAGGED) return;
    if (g.grid[y][x].state == REVEALED) return;

    if (g.first_click) {
        place_mines(x, y);
        g.first_click = false;
        g.start_ticks = neo::timer::get_ticks();
    }

    if (g.grid[y][x].mine) {
        g.game_over = true;
        g.won = false;
        // Reveal all mines
        for (int ry = 0; ry < g.height; ry++)
            for (int rx = 0; rx < g.width; rx++)
                if (g.grid[ry][rx].mine)
                    g.grid[ry][rx].state = REVEALED;
        return;
    }

    flood_fill(x, y);
    check_win();
}

void chord_reveal(int x, int y) {
    if (g.grid[y][x].state != REVEALED) return;
    if (g.grid[y][x].adjacent == 0) return;
    // Count adjacent flags
    int flag_count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < g.width && ny >= 0 && ny < g.height) {
                if (g.grid[ny][nx].state == FLAGGED) flag_count++;
            }
        }
    }
    if (flag_count == g.grid[y][x].adjacent) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < g.width && ny >= 0 && ny < g.height) {
                    if (g.grid[ny][nx].state == HIDDEN) reveal_cell(nx, ny);
                }
            }
        }
    }
}

void toggle_flag(int x, int y) {
    if (g.game_over) return;
    if (g.grid[y][x].state == REVEALED) return;
    if (g.grid[y][x].state == FLAGGED) {
        g.grid[y][x].state = HIDDEN;
        g.flags_placed--;
    } else {
        g.grid[y][x].state = FLAGGED;
        g.flags_placed++;
    }
}

void set_num_color(int n) {
    switch (n) {
        case 1: neo::display::set_fg(12); break; // blue
        case 2: neo::display::set_fg(10); break; // green
        case 3: neo::display::set_fg(9);  break; // red
        case 4: neo::display::set_fg(5);  break; // dark blue
        case 5: neo::display::set_fg(1);  break; // dark red
        case 6: neo::display::set_fg(14); break; // cyan
        case 7: neo::display::set_fg(0);  break; // black
        case 8: neo::display::set_fg(8);  break; // gray
        default: neo::display::set_fg(7); break;
    }
}

// Grid drawing offset
const int GRID_X = 2;
const int GRID_Y = 3;

void draw_cell(int x, int y) {
    int sx = GRID_X + x * 2;
    int sy = GRID_Y + y;
    neo::display::set_cursor(sx, sy);

    Cell& c = g.grid[y][x];
    bool is_cursor = (x == g.cursor_x && y == g.cursor_y);

    if (is_cursor) {
        neo::display::set_bg(6); // highlight cursor
    }

    if (c.state == HIDDEN) {
        neo::display::set_fg(8);
        neo::display::putchar('#');
    } else if (c.state == FLAGGED) {
        neo::display::set_fg(11); // yellow
        neo::display::putchar('F');
    } else { // REVEALED
        if (c.mine) {
            neo::display::set_fg(9); // red
            neo::display::putchar('*');
        } else if (c.adjacent == 0) {
            neo::display::set_fg(8);
            neo::display::putchar('.');
        } else {
            set_num_color(c.adjacent);
            neo::display::putchar('0' + c.adjacent);
        }
    }

    neo::display::set_bg(0);
    neo::display::set_fg(7);
    neo::display::putchar(' ');
}

void draw_board() {
    for (int y = 0; y < g.height; y++) {
        for (int x = 0; x < g.width; x++) {
            draw_cell(x, y);
        }
    }
}

void draw_header() {
    neo::display::set_cursor(0, 0);
    neo::display::set_fg(11);
    neo::display::set_bold(true);

    const char* diff_names[] = { "Easy", "Medium", "Hard" };
    neo::display::printf("  NeoMines [%s]", diff_names[g.difficulty]);
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    neo::display::set_cursor(0, 1);
    int mines_left = g.total_mines - g.flags_placed;
    neo::display::set_fg(9);
    neo::display::printf("  Mines: %d  ", mines_left);
    neo::display::set_fg(14);
    neo::display::printf("Time: %d  ", g.elapsed_secs);
    neo::display::set_fg(7);

    if (g.game_over) {
        if (g.won) {
            neo::display::set_fg(10);
            neo::display::printf("** YOU WIN! **");
        } else {
            neo::display::set_fg(9);
            neo::display::printf("** GAME OVER **");
        }
        neo::display::set_fg(7);
    }
}

void draw_help() {
    int hy = GRID_Y + g.height + 1;
    neo::display::set_cursor(0, hy);
    neo::display::set_fg(8);
    neo::display::printf("  Arrows=Move  Space=Reveal  F=Flag  C=Chord  N=New  1/2/3=Difficulty  Q=Quit");
    neo::display::set_fg(7);
}

void draw_scores() {
    int hy = GRID_Y + g.height + 3;
    neo::display::set_cursor(0, hy);
    neo::display::set_fg(11);
    neo::display::printf("  High Scores:");
    neo::display::set_fg(7);
    const char* names[] = { "Easy", "Medium", "Hard" };
    for (int i = 0; i < 3; i++) {
        neo::display::set_cursor(0, hy + 1 + i);
        if (g.scores[i].valid) {
            neo::display::printf("    %-8s %s  %d sec", names[i], g.scores[i].name, g.scores[i].time_secs);
        } else {
            neo::display::printf("    %-8s ---", names[i]);
        }
    }
}

void full_draw() {
    neo::display::clear();
    draw_header();
    draw_board();
    draw_help();
    draw_scores();
}

void update_timer() {
    if (!g.first_click && !g.game_over) {
        unsigned int now = neo::timer::get_ticks();
        g.elapsed_secs = (int)((now - g.start_ticks) / 50); // ~50 ticks/sec on Amiga
    }
}

void new_game() {
    init_grid();
    full_draw();
}

void record_score() {
    int d = (int)g.difficulty;
    if (g.elapsed_secs < g.scores[d].time_secs) {
        g.scores[d].time_secs = g.elapsed_secs;
        g.scores[d].valid = true;
        neo_strcpy(g.scores[d].name, "Player");
    }
}

int show_menu() {
    neo::display::clear();
    neo::display::set_cursor(10, 3);
    neo::display::set_fg(11);
    neo::display::set_bold(true);
    neo::display::printf("=== NeoMines ===");
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    neo::display::set_cursor(10, 5);
    neo::display::printf("1. Easy   (9x9,  10 mines)");
    neo::display::set_cursor(10, 6);
    neo::display::printf("2. Medium (16x16, 40 mines)");
    neo::display::set_cursor(10, 7);
    neo::display::printf("3. Hard   (30x16, 99 mines)");
    neo::display::set_cursor(10, 9);
    neo::display::printf("Q. Quit");
    neo::display::set_cursor(10, 11);
    neo::display::printf("Select: ");

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, false);
            if (ch == '1') return 0;
            if (ch == '2') return 1;
            if (ch == '3') return 2;
            if (ch == 'q' || ch == 'Q') return -1;
        }
        neo::timer::delay_ms(20);
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    g.rng_state = neo::timer::get_ticks();
    init_scores();

    int choice = show_menu();
    if (choice < 0) return;

    set_difficulty((Difficulty)choice);
    new_game();

    bool running = true;
    int redraw_counter = 0;

    while (running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            // Arrow keys by scancode: up=0x4C, down=0x4D, left=0x4F, right=0x4E
            bool moved = false;

            if (sc == 0x4C && g.cursor_y > 0) { g.cursor_y--; moved = true; }
            else if (sc == 0x4D && g.cursor_y < g.height - 1) { g.cursor_y++; moved = true; }
            else if (sc == 0x4F && g.cursor_x > 0) { g.cursor_x--; moved = true; }
            else if (sc == 0x4E && g.cursor_x < g.width - 1) { g.cursor_x++; moved = true; }
            else if (ch == ' ' || sc == 0x44) { // Space or Return
                if (!g.game_over) {
                    reveal_cell(g.cursor_x, g.cursor_y);
                    if (g.won) record_score();
                }
                full_draw();
            }
            else if (ch == 'f' || ch == 'F') {
                toggle_flag(g.cursor_x, g.cursor_y);
                full_draw();
            }
            else if (ch == 'c' || ch == 'C') {
                chord_reveal(g.cursor_x, g.cursor_y);
                if (g.won) record_score();
                full_draw();
            }
            else if (ch == 'n' || ch == 'N') {
                new_game();
            }
            else if (ch == '1') { set_difficulty(EASY);   new_game(); }
            else if (ch == '2') { set_difficulty(MEDIUM); new_game(); }
            else if (ch == '3') { set_difficulty(HARD);   new_game(); }
            else if (ch == 'q' || ch == 'Q') { running = false; }

            if (moved) {
                draw_board(); // redraw to update cursor
                draw_header();
            }
        }

        update_timer();
        redraw_counter++;
        if (redraw_counter >= 25) { // Update timer display ~every 0.5s
            draw_header();
            redraw_counter = 0;
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    kprintf("Thanks for playing NeoMines!\n");
}
