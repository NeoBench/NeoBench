#include "../include/neobench.h"
#include "../lib/string.h"

// NeoSnake - Snake game for NeoBench

namespace {

const int MAX_SNAKE = 500;
const int MAX_FIELD_W = 78;
const int MAX_FIELD_H = 22;

enum Direction { UP = 0, DOWN, LEFT, RIGHT };

struct Pos {
    int x, y;
};

struct HighScoreEntry {
    int score;
    int difficulty;
    bool valid;
};

struct Game {
    int field_w, field_h;
    Pos snake[MAX_SNAKE];
    int snake_len;
    Direction dir;
    Direction next_dir;
    Pos food;
    int score;
    int level;
    bool game_over;
    bool paused;
    bool wrap_mode;
    int difficulty; // 0=slow, 1=medium, 2=fast
    int base_delay;
    unsigned int rng_state;
    unsigned int last_move_tick;
    HighScoreEntry high_scores[3];
};

Game g;

unsigned int rng_next() {
    g.rng_state = g.rng_state * 1103515245 + 12345;
    return (g.rng_state >> 16) & 0x7FFF;
}

int get_delay() {
    int d = g.base_delay - (g.snake_len / 5) * 2;
    if (d < 30) d = 30;
    return d;
}

bool is_snake(int x, int y) {
    for (int i = 0; i < g.snake_len; i++) {
        if (g.snake[i].x == x && g.snake[i].y == y) return true;
    }
    return false;
}

void spawn_food() {
    int attempts = 0;
    do {
        g.food.x = rng_next() % g.field_w;
        g.food.y = rng_next() % g.field_h;
        attempts++;
    } while (is_snake(g.food.x, g.food.y) && attempts < 1000);
}

void init_game() {
    g.field_w = 40;
    g.field_h = 18;
    g.snake_len = 4;
    int cx = g.field_w / 2;
    int cy = g.field_h / 2;
    for (int i = 0; i < g.snake_len; i++) {
        g.snake[i].x = cx - i;
        g.snake[i].y = cy;
    }
    g.dir = RIGHT;
    g.next_dir = RIGHT;
    g.score = 0;
    g.level = 1;
    g.game_over = false;
    g.paused = false;
    g.last_move_tick = neo::timer::get_ticks();

    switch (g.difficulty) {
        case 0: g.base_delay = 150; break;
        case 1: g.base_delay = 100; break;
        case 2: g.base_delay = 60;  break;
    }

    spawn_food();
}

bool move_snake() {
    g.dir = g.next_dir;
    Pos head = g.snake[0];
    switch (g.dir) {
        case UP:    head.y--; break;
        case DOWN:  head.y++; break;
        case LEFT:  head.x--; break;
        case RIGHT: head.x++; break;
    }

    if (g.wrap_mode) {
        if (head.x < 0) head.x = g.field_w - 1;
        if (head.x >= g.field_w) head.x = 0;
        if (head.y < 0) head.y = g.field_h - 1;
        if (head.y >= g.field_h) head.y = 0;
    } else {
        if (head.x < 0 || head.x >= g.field_w || head.y < 0 || head.y >= g.field_h) {
            g.game_over = true;
            return false;
        }
    }

    // Self collision (skip tail which will move)
    for (int i = 0; i < g.snake_len - 1; i++) {
        if (g.snake[i].x == head.x && g.snake[i].y == head.y) {
            g.game_over = true;
            return false;
        }
    }

    bool ate = (head.x == g.food.x && head.y == g.food.y);

    // Shift body
    if (ate) {
        if (g.snake_len < MAX_SNAKE) {
            // Extend: shift everything right
            for (int i = g.snake_len; i > 0; i--) {
                g.snake[i] = g.snake[i - 1];
            }
            g.snake_len++;
        }
    } else {
        for (int i = g.snake_len - 1; i > 0; i--) {
            g.snake[i] = g.snake[i - 1];
        }
    }
    g.snake[0] = head;

    if (ate) {
        g.score += 10 * g.level;
        g.level = 1 + g.snake_len / 10;
        spawn_food();
    }

    return true;
}

// Drawing
const int BOARD_X = 1;
const int BOARD_Y = 2;

void draw_border() {
    neo::display::set_fg(8);
    // Top
    neo::display::set_cursor(BOARD_X - 1, BOARD_Y - 1);
    neo::display::putchar('+');
    for (int x = 0; x < g.field_w; x++) neo::display::putchar('-');
    neo::display::putchar('+');
    // Bottom
    neo::display::set_cursor(BOARD_X - 1, BOARD_Y + g.field_h);
    neo::display::putchar('+');
    for (int x = 0; x < g.field_w; x++) neo::display::putchar('-');
    neo::display::putchar('+');
    // Sides
    for (int y = 0; y < g.field_h; y++) {
        neo::display::set_cursor(BOARD_X - 1, BOARD_Y + y);
        neo::display::putchar('|');
        neo::display::set_cursor(BOARD_X + g.field_w, BOARD_Y + y);
        neo::display::putchar('|');
    }
    neo::display::set_fg(7);
}

void draw_field() {
    // Clear field area
    for (int y = 0; y < g.field_h; y++) {
        neo::display::set_cursor(BOARD_X, BOARD_Y + y);
        for (int x = 0; x < g.field_w; x++) {
            neo::display::putchar(' ');
        }
    }

    // Draw food
    neo::display::set_cursor(BOARD_X + g.food.x, BOARD_Y + g.food.y);
    neo::display::set_fg(9); // red
    neo::display::putchar('*');

    // Draw snake
    for (int i = 0; i < g.snake_len; i++) {
        neo::display::set_cursor(BOARD_X + g.snake[i].x, BOARD_Y + g.snake[i].y);
        if (i == 0) {
            neo::display::set_fg(10); // green head
            neo::display::set_bold(true);
            switch (g.dir) {
                case UP:    neo::display::putchar('^'); break;
                case DOWN:  neo::display::putchar('v'); break;
                case LEFT:  neo::display::putchar('<'); break;
                case RIGHT: neo::display::putchar('>'); break;
            }
            neo::display::set_bold(false);
        } else {
            neo::display::set_fg(10);
            neo::display::putchar('o');
        }
    }
    neo::display::set_fg(7);
}

void draw_header() {
    neo::display::set_cursor(0, 0);
    neo::display::set_fg(11);
    neo::display::set_bold(true);
    neo::display::printf("  NeoSnake");
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    const char* diff_names[] = { "Slow", "Medium", "Fast" };
    const char* mode = g.wrap_mode ? "Wrap" : "Wall";
    neo::display::printf("  [%s/%s]  Score: %d  Level: %d  Length: %d",
        diff_names[g.difficulty], mode, g.score, g.level, g.snake_len);

    if (g.paused) {
        neo::display::set_fg(11);
        neo::display::printf("  PAUSED");
    }
    if (g.game_over) {
        neo::display::set_fg(9);
        neo::display::printf("  GAME OVER!");
    }
    neo::display::set_fg(7);
}

void draw_help() {
    int y = BOARD_Y + g.field_h + 1;
    neo::display::set_cursor(0, y);
    neo::display::set_fg(8);
    neo::display::printf("  Arrows=Move  P=Pause  W=Toggle Wrap  1/2/3=Speed  N=New  Q=Quit");
    neo::display::set_fg(7);
}

void draw_scores() {
    int x = BOARD_X + g.field_w + 4;
    neo::display::set_cursor(x, BOARD_Y);
    neo::display::set_fg(14);
    neo::display::printf("High Scores:");
    neo::display::set_fg(7);
    const char* names[] = { "Slow", "Medium", "Fast" };
    for (int i = 0; i < 3; i++) {
        neo::display::set_cursor(x, BOARD_Y + 1 + i);
        if (g.high_scores[i].valid) {
            neo::display::printf(" %-7s %d", names[i], g.high_scores[i].score);
        } else {
            neo::display::printf(" %-7s ---", names[i]);
        }
    }
}

void full_draw() {
    neo::display::clear();
    draw_header();
    draw_border();
    draw_field();
    draw_help();
    draw_scores();
}

void record_score() {
    int d = g.difficulty;
    if (!g.high_scores[d].valid || g.score > g.high_scores[d].score) {
        g.high_scores[d].score = g.score;
        g.high_scores[d].valid = true;
    }
}

int show_menu() {
    neo::display::clear();
    neo::display::set_cursor(15, 3);
    neo::display::set_fg(10);
    neo::display::set_bold(true);
    neo::display::printf("=== NeoSnake ===");
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    neo::display::set_cursor(15, 5);
    neo::display::printf("1. Slow");
    neo::display::set_cursor(15, 6);
    neo::display::printf("2. Medium");
    neo::display::set_cursor(15, 7);
    neo::display::printf("3. Fast");
    neo::display::set_cursor(15, 9);
    neo::display::printf("W. Toggle Wrap Mode (%s)", g.wrap_mode ? "ON" : "OFF");
    neo::display::set_cursor(15, 11);
    neo::display::printf("Q. Quit");
    neo::display::set_cursor(15, 13);
    neo::display::printf("Select difficulty: ");

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, false);
            if (ch == '1') return 0;
            if (ch == '2') return 1;
            if (ch == '3') return 2;
            if (ch == 'w' || ch == 'W') {
                g.wrap_mode = !g.wrap_mode;
                neo::display::set_cursor(15, 9);
                neo::display::printf("W. Toggle Wrap Mode (%s) ", g.wrap_mode ? "ON" : "OFF");
            }
            if (ch == 'q' || ch == 'Q') return -1;
        }
        neo::timer::delay_ms(20);
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    g.rng_state = neo::timer::get_ticks();
    g.wrap_mode = false;
    for (int i = 0; i < 3; i++) g.high_scores[i].valid = false;

    int choice = show_menu();
    if (choice < 0) return;
    g.difficulty = choice;

    init_game();
    full_draw();

    bool running = true;
    while (running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            if (ch == 'q' || ch == 'Q') { running = false; continue; }
            if (ch == 'n' || ch == 'N') {
                int c2 = show_menu();
                if (c2 < 0) { running = false; continue; }
                g.difficulty = c2;
                init_game();
                full_draw();
                continue;
            }
            if (ch == 'p' || ch == 'P') {
                g.paused = !g.paused;
                draw_header();
                continue;
            }
            if (ch == 'w' || ch == 'W') {
                g.wrap_mode = !g.wrap_mode;
                draw_header();
                continue;
            }

            if (!g.paused && !g.game_over) {
                if (sc == 0x4C && g.dir != DOWN)  g.next_dir = UP;
                if (sc == 0x4D && g.dir != UP)    g.next_dir = DOWN;
                if (sc == 0x4F && g.dir != RIGHT) g.next_dir = LEFT;
                if (sc == 0x4E && g.dir != LEFT)  g.next_dir = RIGHT;
            }
        }

        if (!g.paused && !g.game_over) {
            unsigned int now = neo::timer::get_ticks();
            int delay_ticks = get_delay() / 20; // convert ms to ~ticks
            if (delay_ticks < 1) delay_ticks = 1;
            if ((now - g.last_move_tick) >= (unsigned int)delay_ticks) {
                g.last_move_tick = now;
                move_snake();
                if (g.game_over) record_score();
                full_draw();
            }
        }

        neo::timer::delay_ms(16);
    }

    neo::display::clear();
    kprintf("Thanks for playing NeoSnake! Score: %d\n", g.score);
}
