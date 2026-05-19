#include "../include/neobench.h"
#include "../lib/string.h"

// NeoTetris - Tetris clone for NeoBench

namespace {

const int FIELD_W = 10;
const int FIELD_H = 20;
const int PIECE_SIZE = 4;

// Piece definitions: 7 tetrominoes, 4 rotations each
// Each piece is a 4x4 grid stored as bitmask
struct PieceDef {
    unsigned short rotations[4]; // 4x4 bitmask per rotation
    int color;
};

// Encode 4x4 grid as 16-bit: bit(row*4+col)
#define B(r,c) (1 << ((r)*4+(c)))

const PieceDef PIECES[7] = {
    // I
    { { B(1,0)|B(1,1)|B(1,2)|B(1,3),
        B(0,2)|B(1,2)|B(2,2)|B(3,2),
        B(2,0)|B(2,1)|B(2,2)|B(2,3),
        B(0,1)|B(1,1)|B(2,1)|B(3,1) }, 14 },
    // O
    { { B(0,1)|B(0,2)|B(1,1)|B(1,2),
        B(0,1)|B(0,2)|B(1,1)|B(1,2),
        B(0,1)|B(0,2)|B(1,1)|B(1,2),
        B(0,1)|B(0,2)|B(1,1)|B(1,2) }, 11 },
    // T
    { { B(0,1)|B(1,0)|B(1,1)|B(1,2),
        B(0,1)|B(1,1)|B(1,2)|B(2,1),
        B(1,0)|B(1,1)|B(1,2)|B(2,1),
        B(0,1)|B(1,0)|B(1,1)|B(2,1) }, 5 },
    // S
    { { B(0,1)|B(0,2)|B(1,0)|B(1,1),
        B(0,1)|B(1,1)|B(1,2)|B(2,2),
        B(1,1)|B(1,2)|B(2,0)|B(2,1),
        B(0,0)|B(1,0)|B(1,1)|B(2,1) }, 10 },
    // Z
    { { B(0,0)|B(0,1)|B(1,1)|B(1,2),
        B(0,2)|B(1,1)|B(1,2)|B(2,1),
        B(1,0)|B(1,1)|B(2,1)|B(2,2),
        B(0,1)|B(1,0)|B(1,1)|B(2,0) }, 9 },
    // J
    { { B(0,0)|B(1,0)|B(1,1)|B(1,2),
        B(0,1)|B(0,2)|B(1,1)|B(2,1),
        B(1,0)|B(1,1)|B(1,2)|B(2,2),
        B(0,1)|B(1,1)|B(2,0)|B(2,1) }, 12 },
    // L
    { { B(0,2)|B(1,0)|B(1,1)|B(1,2),
        B(0,1)|B(1,1)|B(2,1)|B(2,2),
        B(1,0)|B(1,1)|B(1,2)|B(2,0),
        B(0,0)|B(0,1)|B(1,1)|B(2,1) }, 3 },
};

#undef B

bool piece_bit(unsigned short mask, int r, int c) {
    return (mask & (1 << (r * 4 + c))) != 0;
}

struct Game {
    int field[FIELD_H][FIELD_W]; // 0=empty, >0 = color
    int cur_piece, cur_rot, cur_x, cur_y;
    int next_piece;
    int hold_piece;     // -1 = none
    bool hold_used;
    int ghost_y;
    int score, level, lines, combo;
    bool game_over, paused;
    unsigned int drop_timer;
    unsigned int drop_interval; // ticks between drops
    unsigned int last_tick;
    int high_score;
    unsigned int rng_state;
};

Game g;

unsigned int rng_next() {
    g.rng_state = g.rng_state * 1103515245 + 12345;
    return (g.rng_state >> 16) & 0x7FFF;
}

int random_piece() {
    return rng_next() % 7;
}

bool check_collision(int piece, int rot, int px, int py) {
    unsigned short mask = PIECES[piece].rotations[rot];
    for (int r = 0; r < PIECE_SIZE; r++) {
        for (int c = 0; c < PIECE_SIZE; c++) {
            if (!piece_bit(mask, r, c)) continue;
            int fx = px + c;
            int fy = py + r;
            if (fx < 0 || fx >= FIELD_W || fy >= FIELD_H) return true;
            if (fy < 0) continue;
            if (g.field[fy][fx] != 0) return true;
        }
    }
    return false;
}

void calc_ghost() {
    g.ghost_y = g.cur_y;
    while (!check_collision(g.cur_piece, g.cur_rot, g.cur_x, g.ghost_y + 1)) {
        g.ghost_y++;
    }
}

void spawn_piece() {
    g.cur_piece = g.next_piece;
    g.next_piece = random_piece();
    g.cur_rot = 0;
    g.cur_x = FIELD_W / 2 - 2;
    g.cur_y = 0;
    g.hold_used = false;
    g.drop_timer = 0;

    if (check_collision(g.cur_piece, g.cur_rot, g.cur_x, g.cur_y)) {
        g.game_over = true;
    }
    calc_ghost();
}

void lock_piece() {
    unsigned short mask = PIECES[g.cur_piece].rotations[g.cur_rot];
    int color = PIECES[g.cur_piece].color;
    for (int r = 0; r < PIECE_SIZE; r++) {
        for (int c = 0; c < PIECE_SIZE; c++) {
            if (!piece_bit(mask, r, c)) continue;
            int fx = g.cur_x + c;
            int fy = g.cur_y + r;
            if (fy >= 0 && fy < FIELD_H && fx >= 0 && fx < FIELD_W) {
                g.field[fy][fx] = color;
            }
        }
    }
}

int clear_lines() {
    int cleared = 0;
    for (int y = FIELD_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < FIELD_W; x++) {
            if (g.field[y][x] == 0) { full = false; break; }
        }
        if (full) {
            cleared++;
            // Shift down
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < FIELD_W; x++) {
                    g.field[yy][x] = g.field[yy - 1][x];
                }
            }
            for (int x = 0; x < FIELD_W; x++) g.field[0][x] = 0;
            y++; // re-check this row
        }
    }
    return cleared;
}

void add_score(int lines_cleared) {
    const int points[] = { 0, 100, 300, 500, 800 };
    if (lines_cleared > 0 && lines_cleared <= 4) {
        g.score += points[lines_cleared] * (g.level + 1);
    }
    g.lines += lines_cleared;
    g.level = g.lines / 10;
    // Speed up
    g.drop_interval = 30 - g.level * 2;
    if (g.drop_interval < 3) g.drop_interval = 3;
    if (g.score > g.high_score) g.high_score = g.score;
}

bool try_move(int dx, int dy) {
    if (!check_collision(g.cur_piece, g.cur_rot, g.cur_x + dx, g.cur_y + dy)) {
        g.cur_x += dx;
        g.cur_y += dy;
        calc_ghost();
        return true;
    }
    return false;
}

bool try_rotate(int dir) {
    int new_rot = (g.cur_rot + dir + 4) % 4;
    // Wall kick offsets
    const int kicks[][2] = { {0,0}, {-1,0}, {1,0}, {0,-1}, {-2,0}, {2,0} };
    for (int k = 0; k < 6; k++) {
        if (!check_collision(g.cur_piece, new_rot, g.cur_x + kicks[k][0], g.cur_y + kicks[k][1])) {
            g.cur_x += kicks[k][0];
            g.cur_y += kicks[k][1];
            g.cur_rot = new_rot;
            calc_ghost();
            return true;
        }
    }
    return false;
}

void hard_drop() {
    while (!check_collision(g.cur_piece, g.cur_rot, g.cur_x, g.cur_y + 1)) {
        g.cur_y++;
        g.score += 2;
    }
    lock_piece();
    int lc = clear_lines();
    add_score(lc);
    spawn_piece();
}

void hold_piece_fn() {
    if (g.hold_used) return;
    g.hold_used = true;
    if (g.hold_piece == -1) {
        g.hold_piece = g.cur_piece;
        spawn_piece();
    } else {
        int tmp = g.hold_piece;
        g.hold_piece = g.cur_piece;
        g.cur_piece = tmp;
        g.cur_rot = 0;
        g.cur_x = FIELD_W / 2 - 2;
        g.cur_y = 0;
        calc_ghost();
    }
}

void soft_drop() {
    if (try_move(0, 1)) {
        g.score += 1;
    }
}

void gravity_tick() {
    if (!try_move(0, 1)) {
        lock_piece();
        int lc = clear_lines();
        add_score(lc);
        spawn_piece();
    }
}

// Drawing
const int BOARD_X = 2;
const int BOARD_Y = 1;
const int INFO_X = BOARD_X + FIELD_W * 2 + 4;

void draw_block(int sx, int sy, int color) {
    neo::display::set_cursor(sx, sy);
    neo::display::set_fg(color);
    neo::display::printf("[]");
}

void draw_field() {
    // Border
    for (int y = 0; y < FIELD_H; y++) {
        neo::display::set_cursor(BOARD_X - 1, BOARD_Y + y);
        neo::display::set_fg(8);
        neo::display::putchar('|');
        neo::display::set_cursor(BOARD_X + FIELD_W * 2, BOARD_Y + y);
        neo::display::putchar('|');
    }
    neo::display::set_cursor(BOARD_X - 1, BOARD_Y + FIELD_H);
    neo::display::set_fg(8);
    neo::display::putchar('+');
    for (int x = 0; x < FIELD_W * 2; x++) neo::display::putchar('-');
    neo::display::putchar('+');

    // Ghost piece
    unsigned short ghost_mask = PIECES[g.cur_piece].rotations[g.cur_rot];
    // Field contents
    for (int y = 0; y < FIELD_H; y++) {
        for (int x = 0; x < FIELD_W; x++) {
            int sx = BOARD_X + x * 2;
            int sy = BOARD_Y + y;
            if (g.field[y][x] != 0) {
                draw_block(sx, sy, g.field[y][x]);
            } else {
                neo::display::set_cursor(sx, sy);
                neo::display::set_fg(0);
                neo::display::printf("  ");
            }
        }
    }

    // Draw ghost
    if (!g.game_over) {
        for (int r = 0; r < PIECE_SIZE; r++) {
            for (int c = 0; c < PIECE_SIZE; c++) {
                if (!piece_bit(ghost_mask, r, c)) continue;
                int fx = g.cur_x + c;
                int fy = g.ghost_y + r;
                if (fy >= 0 && fy < FIELD_H && fx >= 0 && fx < FIELD_W && g.field[fy][fx] == 0) {
                    neo::display::set_cursor(BOARD_X + fx * 2, BOARD_Y + fy);
                    neo::display::set_fg(8);
                    neo::display::printf("::");
                }
            }
        }
    }

    // Draw current piece
    if (!g.game_over) {
        unsigned short mask = PIECES[g.cur_piece].rotations[g.cur_rot];
        int color = PIECES[g.cur_piece].color;
        for (int r = 0; r < PIECE_SIZE; r++) {
            for (int c = 0; c < PIECE_SIZE; c++) {
                if (!piece_bit(mask, r, c)) continue;
                int fx = g.cur_x + c;
                int fy = g.cur_y + r;
                if (fy >= 0 && fy < FIELD_H && fx >= 0 && fx < FIELD_W) {
                    draw_block(BOARD_X + fx * 2, BOARD_Y + fy, color);
                }
            }
        }
    }
    neo::display::set_fg(7);
}

void draw_piece_preview(int piece, int x, int y) {
    if (piece < 0) {
        for (int r = 0; r < 2; r++) {
            neo::display::set_cursor(x, y + r);
            neo::display::printf("        ");
        }
        return;
    }
    unsigned short mask = PIECES[piece].rotations[0];
    int color = PIECES[piece].color;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            neo::display::set_cursor(x + c * 2, y + r);
            if (piece_bit(mask, r, c)) {
                draw_block(x + c * 2, y + r, color);
            } else {
                neo::display::printf("  ");
            }
        }
    }
    neo::display::set_fg(7);
}

void draw_info() {
    int x = INFO_X;
    neo::display::set_cursor(x, 1);
    neo::display::set_fg(11);
    neo::display::set_bold(true);
    neo::display::printf("NeoTetris");
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    neo::display::set_cursor(x, 3);
    neo::display::printf("Score: %d    ", g.score);
    neo::display::set_cursor(x, 4);
    neo::display::printf("Level: %d    ", g.level);
    neo::display::set_cursor(x, 5);
    neo::display::printf("Lines: %d    ", g.lines);
    neo::display::set_cursor(x, 6);
    neo::display::printf("High:  %d    ", g.high_score);

    neo::display::set_cursor(x, 8);
    neo::display::set_fg(14);
    neo::display::printf("NEXT:");
    neo::display::set_fg(7);
    draw_piece_preview(g.next_piece, x, 9);

    neo::display::set_cursor(x, 14);
    neo::display::set_fg(14);
    neo::display::printf("HOLD:");
    neo::display::set_fg(7);
    draw_piece_preview(g.hold_piece, x, 15);

    if (g.paused) {
        neo::display::set_cursor(x, 20);
        neo::display::set_fg(11);
        neo::display::printf("** PAUSED **");
        neo::display::set_fg(7);
    }
    if (g.game_over) {
        neo::display::set_cursor(x, 20);
        neo::display::set_fg(9);
        neo::display::printf("** GAME OVER **");
        neo::display::set_fg(7);
    }
}

void draw_help() {
    int y = BOARD_Y + FIELD_H + 1;
    neo::display::set_cursor(0, y);
    neo::display::set_fg(8);
    neo::display::printf("  Left/Right=Move  Up=Rotate  Down=Soft Drop  Space=Hard Drop  H=Hold  P=Pause  N=New  Q=Quit");
    neo::display::set_fg(7);
}

void full_draw() {
    neo::display::clear();
    draw_field();
    draw_info();
    draw_help();
}

void init_game() {
    for (int y = 0; y < FIELD_H; y++)
        for (int x = 0; x < FIELD_W; x++)
            g.field[y][x] = 0;
    g.score = 0;
    g.level = 0;
    g.lines = 0;
    g.combo = 0;
    g.game_over = false;
    g.paused = false;
    g.hold_piece = -1;
    g.hold_used = false;
    g.drop_interval = 30;
    g.drop_timer = 0;
    g.next_piece = random_piece();
    spawn_piece();
    g.last_tick = neo::timer::get_ticks();
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    g.rng_state = neo::timer::get_ticks();
    g.high_score = 0;

    init_game();
    full_draw();

    bool running = true;
    while (running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            if (ch == 'q' || ch == 'Q') { running = false; continue; }
            if (ch == 'n' || ch == 'N') { init_game(); full_draw(); continue; }

            if (ch == 'p' || ch == 'P') {
                g.paused = !g.paused;
                full_draw();
                continue;
            }

            if (g.paused || g.game_over) continue;

            if (sc == 0x4F) try_move(-1, 0);       // Left
            else if (sc == 0x4E) try_move(1, 0);    // Right
            else if (sc == 0x4D) soft_drop();        // Down
            else if (sc == 0x4C) try_rotate(1);      // Up = rotate CW
            else if (ch == 'z' || ch == 'Z') try_rotate(-1); // rotate CCW
            else if (ch == ' ') hard_drop();
            else if (ch == 'h' || ch == 'H') hold_piece_fn();

            full_draw();
        }

        if (!g.paused && !g.game_over) {
            unsigned int now = neo::timer::get_ticks();
            unsigned int elapsed = now - g.last_tick;
            g.last_tick = now;
            g.drop_timer += elapsed;
            if (g.drop_timer >= g.drop_interval) {
                g.drop_timer = 0;
                gravity_tick();
                full_draw();
            }
        }

        neo::timer::delay_ms(16);
    }

    neo::display::clear();
    kprintf("Thanks for playing NeoTetris! Final score: %d\n", g.score);
}
