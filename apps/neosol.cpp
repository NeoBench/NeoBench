#include "../include/neobench.h"
#include "../lib/string.h"

// NeoSol - Klondike Solitaire for NeoBench

namespace {

enum Suit { SPADES = 0, HEARTS, DIAMONDS, CLUBS };
enum Color { BLACK = 0, RED };

struct Card {
    int value;  // 1=Ace, 2-10, 11=J, 12=Q, 13=K
    Suit suit;
    bool face_up;
};

const int DECK_SIZE = 52;
const int TABLEAU_COLS = 7;
const int MAX_PILE = 24;
const int STOCK_MAX = 24;

struct Pile {
    Card cards[MAX_PILE];
    int count;
};

struct Game {
    Pile tableau[TABLEAU_COLS];
    Pile foundation[4];
    Card stock[STOCK_MAX];
    int stock_count;
    Card waste[STOCK_MAX];
    int waste_count;
    int stock_pos; // current position in stock
    bool draw_three;
    int cursor_pile;  // 0-6=tableau, 7-10=foundation, 11=stock, 12=waste
    int cursor_card;  // index in pile for pickup
    int sel_pile;     // selected source pile (-1=none)
    int sel_card;     // selected card index
    int moves;
    int score;
    bool won;
    unsigned int rng_state;
};

Game g;

unsigned int rng_next() {
    g.rng_state = g.rng_state * 1103515245 + 12345;
    return (g.rng_state >> 16) & 0x7FFF;
}

Color suit_color(Suit s) {
    return (s == HEARTS || s == DIAMONDS) ? RED : BLACK;
}

char suit_char(Suit s) {
    const char chars[] = "SHDC";
    return chars[(int)s];
}

void card_str(Card& c, char* buf) {
    const char* vals[] = { "?","A","2","3","4","5","6","7","8","9","10","J","Q","K" };
    if (!c.face_up) {
        buf[0] = '['; buf[1] = ']'; buf[2] = 0;
        return;
    }
    int i = 0;
    const char* v = vals[c.value];
    while (*v) buf[i++] = *v++;
    buf[i++] = suit_char(c.suit);
    buf[i] = 0;
}

void create_deck(Card deck[52]) {
    int idx = 0;
    for (int s = 0; s < 4; s++) {
        for (int v = 1; v <= 13; v++) {
            deck[idx].value = v;
            deck[idx].suit = (Suit)s;
            deck[idx].face_up = false;
            idx++;
        }
    }
}

void shuffle_deck(Card deck[52]) {
    for (int i = 51; i > 0; i--) {
        int j = rng_next() % (i + 1);
        Card tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
}

void deal() {
    Card deck[52];
    create_deck(deck);
    shuffle_deck(deck);

    int idx = 0;
    // Deal tableau
    for (int col = 0; col < TABLEAU_COLS; col++) {
        g.tableau[col].count = 0;
    }
    for (int row = 0; row < TABLEAU_COLS; row++) {
        for (int col = row; col < TABLEAU_COLS; col++) {
            Card& c = deck[idx++];
            if (col == row) c.face_up = true;
            g.tableau[col].cards[g.tableau[col].count++] = c;
        }
    }

    // Rest to stock
    g.stock_count = 0;
    while (idx < 52) {
        g.stock[g.stock_count++] = deck[idx++];
    }
    g.waste_count = 0;
    g.stock_pos = 0;

    for (int i = 0; i < 4; i++) g.foundation[i].count = 0;

    g.cursor_pile = 0;
    g.cursor_card = 0;
    g.sel_pile = -1;
    g.moves = 0;
    g.score = 0;
    g.won = false;
}

void flip_top_tableau(int col) {
    if (g.tableau[col].count > 0) {
        Card& top = g.tableau[col].cards[g.tableau[col].count - 1];
        if (!top.face_up) {
            top.face_up = true;
            g.score += 5;
        }
    }
}

bool can_place_tableau(Card& card, int col) {
    if (g.tableau[col].count == 0) {
        return card.value == 13; // Only kings on empty
    }
    Card& top = g.tableau[col].cards[g.tableau[col].count - 1];
    if (!top.face_up) return false;
    return (suit_color(card.suit) != suit_color(top.suit)) && (card.value == top.value - 1);
}

bool can_place_foundation(Card& card, int f) {
    if (g.foundation[f].count == 0) {
        return card.value == 1; // Only aces
    }
    Card& top = g.foundation[f].cards[g.foundation[f].count - 1];
    return (card.suit == top.suit) && (card.value == top.value + 1);
}

int find_foundation_for(Card& card) {
    for (int f = 0; f < 4; f++) {
        if (can_place_foundation(card, f)) return f;
    }
    return -1;
}

void draw_stock() {
    if (g.stock_count == 0) {
        // Recycle waste to stock
        while (g.waste_count > 0) {
            g.stock[g.stock_count++] = g.waste[--g.waste_count];
            g.stock[g.stock_count - 1].face_up = false;
        }
        g.score -= 20;
        if (g.score < 0) g.score = 0;
        return;
    }
    int draw = g.draw_three ? 3 : 1;
    if (draw > g.stock_count) draw = g.stock_count;
    for (int i = 0; i < draw; i++) {
        Card c = g.stock[--g.stock_count];
        c.face_up = true;
        g.waste[g.waste_count++] = c;
    }
}

bool move_waste_to_tableau(int col) {
    if (g.waste_count == 0) return false;
    Card& c = g.waste[g.waste_count - 1];
    if (!can_place_tableau(c, col)) return false;
    g.tableau[col].cards[g.tableau[col].count++] = c;
    g.waste_count--;
    g.score += 5;
    g.moves++;
    return true;
}

bool move_waste_to_foundation() {
    if (g.waste_count == 0) return false;
    Card& c = g.waste[g.waste_count - 1];
    int f = find_foundation_for(c);
    if (f < 0) return false;
    g.foundation[f].cards[g.foundation[f].count++] = c;
    g.waste_count--;
    g.score += 10;
    g.moves++;
    return true;
}

bool move_tableau_to_foundation(int col) {
    if (g.tableau[col].count == 0) return false;
    Card& c = g.tableau[col].cards[g.tableau[col].count - 1];
    if (!c.face_up) return false;
    int f = find_foundation_for(c);
    if (f < 0) return false;
    g.foundation[f].cards[g.foundation[f].count++] = c;
    g.tableau[col].count--;
    flip_top_tableau(col);
    g.score += 10;
    g.moves++;
    return true;
}

bool move_tableau_stack(int from_col, int card_idx, int to_col) {
    if (from_col == to_col) return false;
    if (card_idx < 0 || card_idx >= g.tableau[from_col].count) return false;
    Card& c = g.tableau[from_col].cards[card_idx];
    if (!c.face_up) return false;
    if (!can_place_tableau(c, to_col)) return false;

    int move_count = g.tableau[from_col].count - card_idx;
    for (int i = 0; i < move_count; i++) {
        g.tableau[to_col].cards[g.tableau[to_col].count++] = g.tableau[from_col].cards[card_idx + i];
    }
    g.tableau[from_col].count = card_idx;
    flip_top_tableau(from_col);
    g.moves++;
    return true;
}

void check_win() {
    int total = 0;
    for (int f = 0; f < 4; f++) total += g.foundation[f].count;
    if (total == 52) g.won = true;
}

void auto_complete() {
    bool moved = true;
    while (moved) {
        moved = false;
        // Try waste to foundation
        if (move_waste_to_foundation()) { moved = true; continue; }
        // Try each tableau to foundation
        for (int col = 0; col < TABLEAU_COLS; col++) {
            if (move_tableau_to_foundation(col)) { moved = true; break; }
        }
    }
    check_win();
}

bool can_auto_complete() {
    // All cards face up and stock empty
    if (g.stock_count > 0 || g.waste_count > 0) return false;
    for (int col = 0; col < TABLEAU_COLS; col++) {
        for (int i = 0; i < g.tableau[col].count; i++) {
            if (!g.tableau[col].cards[i].face_up) return false;
        }
    }
    return true;
}

// Drawing constants
const int BOARD_X = 1;
const int BOARD_Y = 2;
const int CARD_W = 5;

void draw_card_at(int x, int y, Card* c, bool highlight) {
    neo::display::set_cursor(x, y);
    if (highlight) neo::display::set_bg(4);
    if (c == nullptr) {
        neo::display::set_fg(8);
        neo::display::printf(" .. ");
    } else if (!c->face_up) {
        neo::display::set_fg(5);
        neo::display::printf("[##]");
    } else {
        if (suit_color(c->suit) == RED) {
            neo::display::set_fg(9);
        } else {
            neo::display::set_fg(15);
        }
        char buf[8];
        card_str(*c, buf);
        int len = neo_strlen(buf);
        neo::display::printf("%-4s", buf);
    }
    neo::display::set_bg(0);
    neo::display::set_fg(7);
}

void draw_header_line() {
    neo::display::set_cursor(0, 0);
    neo::display::set_fg(11);
    neo::display::set_bold(true);
    neo::display::printf("  NeoSol - Klondike Solitaire");
    neo::display::set_bold(false);
    neo::display::set_fg(7);
    neo::display::printf("   Moves: %d  Score: %d", g.moves, g.score);
    if (g.won) {
        neo::display::set_fg(10);
        neo::display::printf("  ** YOU WIN! **");
        neo::display::set_fg(7);
    }
}

void draw_top_row() {
    int y = BOARD_Y;
    // Stock
    bool hl = (g.cursor_pile == 11);
    if (g.stock_count > 0) {
        Card fake;
        fake.face_up = false;
        draw_card_at(BOARD_X, y, &fake, hl);
    } else {
        neo::display::set_cursor(BOARD_X, y);
        if (hl) neo::display::set_bg(4);
        neo::display::set_fg(8);
        neo::display::printf(" <> ");
        neo::display::set_bg(0);
    }
    neo::display::set_fg(8);
    neo::display::printf(" ");

    // Waste
    hl = (g.cursor_pile == 12);
    if (g.waste_count > 0) {
        draw_card_at(BOARD_X + CARD_W + 1, y, &g.waste[g.waste_count - 1], hl);
    } else {
        draw_card_at(BOARD_X + CARD_W + 1, y, nullptr, hl);
    }

    // Foundations
    for (int f = 0; f < 4; f++) {
        int fx = BOARD_X + (f + 3) * (CARD_W + 1);
        hl = (g.cursor_pile == 7 + f);
        if (g.foundation[f].count > 0) {
            draw_card_at(fx, y, &g.foundation[f].cards[g.foundation[f].count - 1], hl);
        } else {
            neo::display::set_cursor(fx, y);
            if (hl) neo::display::set_bg(4);
            const char* suit_labels[] = { " _S ", " _H ", " _D ", " _C " };
            neo::display::set_fg(8);
            neo::display::printf("%s", suit_labels[f]);
            neo::display::set_bg(0);
        }
    }
    neo::display::set_fg(7);
}

void draw_tableau() {
    int start_y = BOARD_Y + 2;
    // Find max column height
    int max_h = 0;
    for (int col = 0; col < TABLEAU_COLS; col++) {
        if (g.tableau[col].count > max_h) max_h = g.tableau[col].count;
    }
    if (max_h < 1) max_h = 1;

    for (int row = 0; row < max_h + 1; row++) {
        for (int col = 0; col < TABLEAU_COLS; col++) {
            int x = BOARD_X + col * (CARD_W + 1);
            int y = start_y + row;

            bool is_cursor = (g.cursor_pile == col && g.cursor_card == row);
            bool is_selected = (g.sel_pile == col && row >= g.sel_card && row < g.tableau[col].count);

            if (row < g.tableau[col].count) {
                bool hl = is_cursor || is_selected;
                draw_card_at(x, y, &g.tableau[col].cards[row], hl);
            } else if (row == 0 && g.tableau[col].count == 0) {
                neo::display::set_cursor(x, y);
                if (is_cursor) neo::display::set_bg(4);
                neo::display::set_fg(8);
                neo::display::printf(" __ ");
                neo::display::set_bg(0);
            } else {
                neo::display::set_cursor(x, y);
                neo::display::printf("    ");
            }
        }
    }
    neo::display::set_fg(7);
}

void draw_help_bar() {
    int y = neo::display::get_height() - 2;
    neo::display::set_cursor(0, y);
    neo::display::set_fg(8);
    neo::display::printf("  Arrows=Move  Enter=Select/Place  D=Draw  A=Auto  N=New  Q=Quit");
    neo::display::set_fg(7);
}

void full_draw() {
    neo::display::clear();
    draw_header_line();
    draw_top_row();
    draw_tableau();
    draw_help_bar();
}

void celebration() {
    for (int i = 0; i < 10; i++) {
        int x = rng_next() % neo::display::get_width();
        int y = rng_next() % neo::display::get_height();
        neo::display::set_cursor(x, y);
        neo::display::set_fg(9 + (i % 7));
        neo::display::putchar('*');
        neo::timer::delay_ms(100);
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    g.rng_state = neo::timer::get_ticks();
    g.draw_three = false;

    deal();
    full_draw();

    bool running = true;
    while (running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            if (sc == 0x4F) { // Left
                if (g.cursor_pile > 0 && g.cursor_pile <= 6) g.cursor_pile--;
                else if (g.cursor_pile == 11) {} // leftmost
                else if (g.cursor_pile == 12) g.cursor_pile = 11;
                else if (g.cursor_pile >= 7 && g.cursor_pile <= 10) {
                    if (g.cursor_pile == 7) g.cursor_pile = 12;
                    else g.cursor_pile--;
                }
            }
            else if (sc == 0x4E) { // Right
                if (g.cursor_pile < 6 && g.cursor_pile >= 0) g.cursor_pile++;
                else if (g.cursor_pile == 11) g.cursor_pile = 12;
                else if (g.cursor_pile == 12) g.cursor_pile = 7;
                else if (g.cursor_pile >= 7 && g.cursor_pile < 10) g.cursor_pile++;
            }
            else if (sc == 0x4C) { // Up
                if (g.cursor_pile >= 0 && g.cursor_pile <= 6) {
                    if (g.cursor_card > 0) g.cursor_card--;
                    else {
                        // Move to top row
                        if (g.cursor_pile <= 1) g.cursor_pile = 11;
                        else if (g.cursor_pile == 2) g.cursor_pile = 12;
                        else g.cursor_pile = 7 + (g.cursor_pile - 3);
                    }
                }
            }
            else if (sc == 0x4D) { // Down
                if (g.cursor_pile >= 0 && g.cursor_pile <= 6) {
                    int max_c = g.tableau[g.cursor_pile].count - 1;
                    if (max_c < 0) max_c = 0;
                    if (g.cursor_card < max_c) g.cursor_card++;
                } else {
                    // Move from top row to tableau
                    int col = 0;
                    if (g.cursor_pile == 11 || g.cursor_pile == 12) col = 0;
                    else if (g.cursor_pile >= 7) col = g.cursor_pile - 7 + 3;
                    if (col > 6) col = 6;
                    g.cursor_pile = col;
                    g.cursor_card = 0;
                }
            }
            else if (sc == 0x44 || ch == ' ') { // Enter/Space = select or place
                if (g.sel_pile == -1) {
                    // Select
                    if (g.cursor_pile >= 0 && g.cursor_pile <= 6) {
                        if (g.tableau[g.cursor_pile].count > 0 &&
                            g.cursor_card < g.tableau[g.cursor_pile].count &&
                            g.tableau[g.cursor_pile].cards[g.cursor_card].face_up) {
                            g.sel_pile = g.cursor_pile;
                            g.sel_card = g.cursor_card;
                        }
                    } else if (g.cursor_pile == 12 && g.waste_count > 0) {
                        g.sel_pile = 12;
                        g.sel_card = g.waste_count - 1;
                    } else if (g.cursor_pile == 11) {
                        draw_stock();
                    }
                } else {
                    // Place
                    bool placed = false;
                    if (g.cursor_pile >= 0 && g.cursor_pile <= 6) {
                        if (g.sel_pile >= 0 && g.sel_pile <= 6) {
                            placed = move_tableau_stack(g.sel_pile, g.sel_card, g.cursor_pile);
                        } else if (g.sel_pile == 12) {
                            placed = move_waste_to_tableau(g.cursor_pile);
                        }
                    } else if (g.cursor_pile >= 7 && g.cursor_pile <= 10) {
                        if (g.sel_pile >= 0 && g.sel_pile <= 6) {
                            placed = move_tableau_to_foundation(g.sel_pile);
                        } else if (g.sel_pile == 12) {
                            placed = move_waste_to_foundation();
                        }
                    }
                    g.sel_pile = -1;
                    if (placed) check_win();
                }
            }
            else if (ch == 'd' || ch == 'D') {
                draw_stock();
            }
            else if (ch == 'a' || ch == 'A') {
                if (can_auto_complete()) auto_complete();
            }
            else if (ch == 'n' || ch == 'N') {
                deal();
            }
            else if (ch == 'q' || ch == 'Q') {
                running = false;
            }

            full_draw();
            if (g.won) celebration();
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    kprintf("Thanks for playing NeoSol!\n");
}
