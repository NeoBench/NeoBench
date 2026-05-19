#include "../include/neobench.h"
#include "../lib/string.h"

// NeoChess - Chess with AI for NeoBench

namespace {

enum Piece { EMPTY=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6 };
enum Side { WHITE=0, BLACK=1 };

// Board stored as piece|side: positive=white, negative=black
// Or better: separate piece and color
struct Square {
    Piece piece;
    Side side;
    bool occupied;
};

struct Move {
    int from_r, from_c, to_r, to_c;
    Piece promotion; // EMPTY if none
    bool is_castle_king;
    bool is_castle_queen;
    bool is_en_passant;
};

const int MAX_MOVES = INODE_SIZE;
const int MAX_HISTORY = 200;

struct Board {
    Square sq[8][8];
    Side to_move;
    bool castle_rights[2][2]; // [side][0=kingside,1=queenside]
    int en_passant_col; // -1 if none
    int halfmove;
    int fullmove;
};

struct HistoryEntry {
    Move move;
    Board board_before;
};

struct Game {
    Board board;
    HistoryEntry history[MAX_HISTORY];
    int history_count;
    int ai_depth;
    bool player_is_white;
    bool game_over;
    char status_msg[64];
    char input_buf[32];
    int input_len;
    bool show_thinking;
};

Game g;

// Piece-square tables for evaluation (simplified, white perspective, index [row][col])
// Values in centipawns
const int PAWN_TABLE[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0 },
    { 50, 50, 50, 50, 50, 50, 50, 50 },
    { 10, 10, 20, 30, 30, 20, 10, 10 },
    {  5,  5, 10, 25, 25, 10,  5,  5 },
    {  0,  0,  0, 20, 20,  0,  0,  0 },
    {  5, -5,-10,  0,  0,-10, -5,  5 },
    {  5, 10, 10,-20,-20, 10, 10,  5 },
    {  0,  0,  0,  0,  0,  0,  0,  0 }
};

const int KNIGHT_TABLE[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50 },
    {-40,-20,  0,  0,  0,  0,-20,-40 },
    {-30,  0, 10, 15, 15, 10,  0,-30 },
    {-30,  5, 15, 20, 20, 15,  5,-30 },
    {-30,  0, 15, 20, 20, 15,  0,-30 },
    {-30,  5, 10, 15, 15, 10,  5,-30 },
    {-40,-20,  0,  5,  5,  0,-20,-40 },
    {-50,-40,-30,-30,-30,-30,-40,-50 }
};

const int PIECE_VALUES[7] = { 0, 100, 320, 330, 500, 900, 20000 };

char piece_char(Piece p, Side s) {
    const char white_chars[] = ".PNBRQK";
    const char black_chars[] = ".pnbrqk";
    if (s == WHITE) return white_chars[(int)p];
    return black_chars[(int)p];
}

char piece_display(Piece p, Side s) {
    // Using ASCII art characters for display
    const char white_chars[] = " PNBRQK";
    const char black_chars[] = " pnbrqk";
    if (s == WHITE) return white_chars[(int)p];
    return black_chars[(int)p];
}

void init_board(Board& b) {
    // Clear
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            b.sq[r][c].piece = EMPTY;
            b.sq[r][c].occupied = false;
        }

    // Place pieces
    Piece back_rank[] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
    for (int c = 0; c < 8; c++) {
        // Black back rank (row 0)
        b.sq[0][c].piece = back_rank[c];
        b.sq[0][c].side = BLACK;
        b.sq[0][c].occupied = true;
        // Black pawns
        b.sq[1][c].piece = PAWN;
        b.sq[1][c].side = BLACK;
        b.sq[1][c].occupied = true;
        // White pawns
        b.sq[6][c].piece = PAWN;
        b.sq[6][c].side = WHITE;
        b.sq[6][c].occupied = true;
        // White back rank (row 7)
        b.sq[7][c].piece = back_rank[c];
        b.sq[7][c].side = WHITE;
        b.sq[7][c].occupied = true;
    }

    b.to_move = WHITE;
    b.castle_rights[WHITE][0] = true; // kingside
    b.castle_rights[WHITE][1] = true; // queenside
    b.castle_rights[BLACK][0] = true;
    b.castle_rights[BLACK][1] = true;
    b.en_passant_col = -1;
    b.halfmove = 0;
    b.fullmove = 1;
}

bool in_bounds(int r, int c) {
    return r >= 0 && r < 8 && c >= 0 && c < 8;
}

// Find king position
void find_king(Board& b, Side s, int& kr, int& kc) {
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (b.sq[r][c].occupied && b.sq[r][c].piece == KING && b.sq[r][c].side == s) {
                kr = r; kc = c; return;
            }
    kr = -1; kc = -1;
}

bool is_attacked(Board& b, int tr, int tc, Side by_side) {
    // Check if square (tr,tc) is attacked by 'by_side'
    // Knight attacks
    const int kn_dr[] = {-2,-2,-1,-1,1,1,2,2};
    const int kn_dc[] = {-1,1,-2,2,-2,2,-1,1};
    for (int i = 0; i < 8; i++) {
        int r = tr + kn_dr[i], c = tc + kn_dc[i];
        if (in_bounds(r,c) && b.sq[r][c].occupied && b.sq[r][c].side == by_side && b.sq[r][c].piece == KNIGHT)
            return true;
    }

    // King attacks
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int r = tr+dr, c = tc+dc;
            if (in_bounds(r,c) && b.sq[r][c].occupied && b.sq[r][c].side == by_side && b.sq[r][c].piece == KING)
                return true;
        }

    // Pawn attacks
    int pawn_dir = (by_side == WHITE) ? 1 : -1; // direction pawns attack FROM
    for (int dc = -1; dc <= 1; dc += 2) {
        int r = tr + pawn_dir, c = tc + dc;
        if (in_bounds(r,c) && b.sq[r][c].occupied && b.sq[r][c].side == by_side && b.sq[r][c].piece == PAWN)
            return true;
    }

    // Sliding pieces (bishop/queen diagonals, rook/queen straights)
    const int dirs[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    for (int d = 0; d < 8; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int r = tr + dirs[d][0]*dist, c = tc + dirs[d][1]*dist;
            if (!in_bounds(r,c)) break;
            if (b.sq[r][c].occupied) {
                if (b.sq[r][c].side == by_side) {
                    Piece p = b.sq[r][c].piece;
                    if (d < 4) { // straight
                        if (p == ROOK || p == QUEEN) return true;
                    } else { // diagonal
                        if (p == BISHOP || p == QUEEN) return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

bool in_check(Board& b, Side s) {
    int kr, kc;
    find_king(b, s, kr, kc);
    if (kr < 0) return true;
    return is_attacked(b, kr, kc, s == WHITE ? BLACK : WHITE);
}

void make_move(Board& b, Move& m) {
    Square& from = b.sq[m.from_r][m.from_c];
    Square& to = b.sq[m.to_r][m.to_c];

    // Handle castling
    if (m.is_castle_king) {
        int r = m.from_r;
        b.sq[r][6] = b.sq[r][4]; // king
        b.sq[r][5] = b.sq[r][7]; // rook
        b.sq[r][4].occupied = false; b.sq[r][4].piece = EMPTY;
        b.sq[r][7].occupied = false; b.sq[r][7].piece = EMPTY;
    } else if (m.is_castle_queen) {
        int r = m.from_r;
        b.sq[r][2] = b.sq[r][4];
        b.sq[r][3] = b.sq[r][0];
        b.sq[r][4].occupied = false; b.sq[r][4].piece = EMPTY;
        b.sq[r][0].occupied = false; b.sq[r][0].piece = EMPTY;
    } else {
        // En passant capture
        if (m.is_en_passant) {
            b.sq[m.from_r][m.to_c].occupied = false;
            b.sq[m.from_r][m.to_c].piece = EMPTY;
        }
        to = from;
        from.occupied = false;
        from.piece = EMPTY;

        // Promotion
        if (m.promotion != EMPTY) {
            to.piece = m.promotion;
        }
    }

    // Update en passant
    b.en_passant_col = -1;
    if (from.piece == PAWN || b.sq[m.to_r][m.to_c].piece == PAWN) {
        int dr = m.to_r - m.from_r;
        if (dr == 2 || dr == -2) {
            b.en_passant_col = m.from_c;
        }
    }

    // Update castle rights
    if (m.from_r == 7 && m.from_c == 4) { b.castle_rights[WHITE][0] = false; b.castle_rights[WHITE][1] = false; }
    if (m.from_r == 0 && m.from_c == 4) { b.castle_rights[BLACK][0] = false; b.castle_rights[BLACK][1] = false; }
    if (m.from_r == 7 && m.from_c == 7) b.castle_rights[WHITE][0] = false;
    if (m.from_r == 7 && m.from_c == 0) b.castle_rights[WHITE][1] = false;
    if (m.from_r == 0 && m.from_c == 7) b.castle_rights[BLACK][0] = false;
    if (m.from_r == 0 && m.from_c == 0) b.castle_rights[BLACK][1] = false;
    if (m.to_r == 7 && m.to_c == 7) b.castle_rights[WHITE][0] = false;
    if (m.to_r == 7 && m.to_c == 0) b.castle_rights[WHITE][1] = false;
    if (m.to_r == 0 && m.to_c == 7) b.castle_rights[BLACK][0] = false;
    if (m.to_r == 0 && m.to_c == 0) b.castle_rights[BLACK][1] = false;

    b.to_move = (b.to_move == WHITE) ? BLACK : WHITE;
}

void add_move(Move* moves, int& count, int fr, int fc, int tr, int tc,
              Piece promo = EMPTY, bool castle_k = false, bool castle_q = false, bool ep = false) {
    if (count >= MAX_MOVES) return;
    Move& m = moves[count];
    m.from_r = fr; m.from_c = fc;
    m.to_r = tr; m.to_c = tc;
    m.promotion = promo;
    m.is_castle_king = castle_k;
    m.is_castle_queen = castle_q;
    m.is_en_passant = ep;
    count++;
}

int generate_moves(Board& b, Move* moves) {
    int count = 0;
    Side s = b.to_move;
    Side opp = (s == WHITE) ? BLACK : WHITE;
    int pawn_dir = (s == WHITE) ? -1 : 1;
    int pawn_start = (s == WHITE) ? 6 : 1;
    int pawn_promo = (s == WHITE) ? 0 : 7;

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (!b.sq[r][c].occupied || b.sq[r][c].side != s) continue;
            Piece p = b.sq[r][c].piece;

            if (p == PAWN) {
                // Forward
                int nr = r + pawn_dir;
                if (in_bounds(nr, c) && !b.sq[nr][c].occupied) {
                    if (nr == pawn_promo) {
                        add_move(moves, count, r, c, nr, c, QUEEN);
                        add_move(moves, count, r, c, nr, c, KNIGHT);
                        add_move(moves, count, r, c, nr, c, ROOK);
                        add_move(moves, count, r, c, nr, c, BISHOP);
                    } else {
                        add_move(moves, count, r, c, nr, c);
                        // Double push
                        if (r == pawn_start && !b.sq[r + 2*pawn_dir][c].occupied) {
                            add_move(moves, count, r, c, r + 2*pawn_dir, c);
                        }
                    }
                }
                // Captures
                for (int dc = -1; dc <= 1; dc += 2) {
                    int nc = c + dc;
                    if (!in_bounds(nr, nc)) continue;
                    bool can_capture = (b.sq[nr][nc].occupied && b.sq[nr][nc].side == opp);
                    bool can_ep = (nr == (s == WHITE ? 2 : 5) && b.en_passant_col == nc &&
                                   r == (s == WHITE ? 3 : 4));
                    if (can_capture || can_ep) {
                        if (nr == pawn_promo) {
                            add_move(moves, count, r, c, nr, nc, QUEEN);
                            add_move(moves, count, r, c, nr, nc, KNIGHT);
                        } else {
                            add_move(moves, count, r, c, nr, nc, EMPTY, false, false, can_ep);
                        }
                    }
                }
            }
            else if (p == KNIGHT) {
                const int kdr[] = {-2,-2,-1,-1,1,1,2,2};
                const int kdc[] = {-1,1,-2,2,-2,2,-1,1};
                for (int i = 0; i < 8; i++) {
                    int nr = r+kdr[i], nc = c+kdc[i];
                    if (!in_bounds(nr,nc)) continue;
                    if (b.sq[nr][nc].occupied && b.sq[nr][nc].side == s) continue;
                    add_move(moves, count, r, c, nr, nc);
                }
            }
            else if (p == BISHOP || p == ROOK || p == QUEEN) {
                int start_d = 0, end_d = 8;
                if (p == BISHOP) { start_d = 4; }
                if (p == ROOK) { end_d = 4; }
                const int dirs[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
                for (int d = start_d; d < end_d; d++) {
                    for (int dist = 1; dist < 8; dist++) {
                        int nr = r+dirs[d][0]*dist, nc = c+dirs[d][1]*dist;
                        if (!in_bounds(nr,nc)) break;
                        if (b.sq[nr][nc].occupied) {
                            if (b.sq[nr][nc].side != s) add_move(moves, count, r, c, nr, nc);
                            break;
                        }
                        add_move(moves, count, r, c, nr, nc);
                    }
                }
            }
            else if (p == KING) {
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = r+dr, nc = c+dc;
                        if (!in_bounds(nr,nc)) continue;
                        if (b.sq[nr][nc].occupied && b.sq[nr][nc].side == s) continue;
                        add_move(moves, count, r, c, nr, nc);
                    }
                // Castling
                int kr = (s == WHITE) ? 7 : 0;
                if (r == kr && c == 4 && !in_check(b, s)) {
                    // Kingside
                    if (b.castle_rights[(int)s][0] && !b.sq[kr][5].occupied && !b.sq[kr][6].occupied &&
                        b.sq[kr][7].occupied && b.sq[kr][7].piece == ROOK &&
                        !is_attacked(b, kr, 5, opp) && !is_attacked(b, kr, 6, opp)) {
                        add_move(moves, count, r, c, kr, 6, EMPTY, true, false);
                    }
                    // Queenside
                    if (b.castle_rights[(int)s][1] && !b.sq[kr][3].occupied && !b.sq[kr][2].occupied &&
                        !b.sq[kr][1].occupied && b.sq[kr][0].occupied && b.sq[kr][0].piece == ROOK &&
                        !is_attacked(b, kr, 3, opp) && !is_attacked(b, kr, 2, opp)) {
                        add_move(moves, count, r, c, kr, 2, EMPTY, false, true);
                    }
                }
            }
        }
    }
    return count;
}

int generate_legal_moves(Board& b, Move* moves) {
    Move pseudo[MAX_MOVES];
    int pcount = generate_moves(b, pseudo);
    int count = 0;
    Side s = b.to_move;

    for (int i = 0; i < pcount; i++) {
        Board copy = b;
        make_move(copy, pseudo[i]);
        if (!in_check(copy, s)) {
            moves[count++] = pseudo[i];
        }
    }
    return count;
}

// Evaluation
int evaluate(Board& b) {
    int score = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (!b.sq[r][c].occupied) continue;
            Piece p = b.sq[r][c].piece;
            int val = PIECE_VALUES[(int)p];

            // Piece-square bonus
            int pst = 0;
            int pr = (b.sq[r][c].side == WHITE) ? r : 7 - r;
            if (p == PAWN) pst = PAWN_TABLE[pr][c];
            else if (p == KNIGHT) pst = KNIGHT_TABLE[pr][c];
            val += pst;

            if (b.sq[r][c].side == WHITE) score += val;
            else score -= val;
        }
    }
    return score;
}

// Minimax with alpha-beta
int alphabeta(Board& b, int depth, int alpha, int beta, bool maximizing) {
    if (depth == 0) return evaluate(b);

    Move moves[MAX_MOVES];
    int count = generate_legal_moves(b, moves);

    if (count == 0) {
        if (in_check(b, b.to_move)) {
            return maximizing ? -100000 + (g.ai_depth - depth) : 100000 - (g.ai_depth - depth);
        }
        return 0; // stalemate
    }

    if (maximizing) {
        int best = -200000;
        for (int i = 0; i < count; i++) {
            Board copy = b;
            make_move(copy, moves[i]);
            int val = alphabeta(copy, depth - 1, alpha, beta, false);
            if (val > best) best = val;
            if (best > alpha) alpha = best;
            if (beta <= alpha) break;
        }
        return best;
    } else {
        int best = 200000;
        for (int i = 0; i < count; i++) {
            Board copy = b;
            make_move(copy, moves[i]);
            int val = alphabeta(copy, depth - 1, alpha, beta, true);
            if (val < best) best = val;
            if (best < beta) beta = best;
            if (beta <= alpha) break;
        }
        return best;
    }
}

Move ai_find_move(Board& b) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(b, moves);
    bool maximizing = (b.to_move == WHITE);
    int best_val = maximizing ? -200000 : 200000;
    int best_idx = 0;

    for (int i = 0; i < count; i++) {
        Board copy = b;
        make_move(copy, moves[i]);
        int val = alphabeta(copy, g.ai_depth - 1, -200000, 200000, !maximizing);
        if (maximizing) {
            if (val > best_val) { best_val = val; best_idx = i; }
        } else {
            if (val < best_val) { best_val = val; best_idx = i; }
        }
    }
    return moves[best_idx];
}

// Move notation helpers
void move_to_str(Move& m, char* buf) {
    buf[0] = 'a' + m.from_c;
    buf[1] = '8' - m.from_r;
    buf[2] = 'a' + m.to_c;
    buf[3] = '8' - m.to_r;
    buf[4] = 0;
    if (m.is_castle_king) { neo_strcpy(buf, "O-O"); }
    else if (m.is_castle_queen) { neo_strcpy(buf, "O-O-O"); }
    else if (m.promotion != EMPTY) {
        const char promo_chars[] = ".pnbrqk";
        buf[4] = promo_chars[(int)m.promotion];
        buf[5] = 0;
    }
}

bool parse_and_find_move(Board& b, const char* input, Move& result) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(b, moves);

    // Handle castling
    if (neo_strcmp(input, "O-O") == 0 || neo_strcmp(input, "o-o") == 0 || neo_strcmp(input, "0-0") == 0) {
        for (int i = 0; i < count; i++) {
            if (moves[i].is_castle_king) { result = moves[i]; return true; }
        }
        return false;
    }
    if (neo_strcmp(input, "O-O-O") == 0 || neo_strcmp(input, "o-o-o") == 0 || neo_strcmp(input, "0-0-0") == 0) {
        for (int i = 0; i < count; i++) {
            if (moves[i].is_castle_queen) { result = moves[i]; return true; }
        }
        return false;
    }

    // Try coordinate notation: e2e4
    int len = neo_strlen(input);
    if (len >= 4) {
        int fc = input[0] - 'a';
        int fr = '8' - input[1];
        int tc = input[2] - 'a';
        int tr = '8' - input[3];
        Piece promo = EMPTY;
        if (len >= 5) {
            char pc = input[4];
            if (pc == 'q' || pc == 'Q') promo = QUEEN;
            else if (pc == 'r' || pc == 'R') promo = ROOK;
            else if (pc == 'b' || pc == 'B') promo = BISHOP;
            else if (pc == 'n' || pc == 'N') promo = KNIGHT;
        }
        for (int i = 0; i < count; i++) {
            if (moves[i].from_r == fr && moves[i].from_c == fc &&
                moves[i].to_r == tr && moves[i].to_c == tc) {
                if (promo != EMPTY && moves[i].promotion != promo) continue;
                result = moves[i];
                return true;
            }
        }
    }

    // Try simple algebraic: e4, Nf3, Bxe5, etc.
    if (len >= 2) {
        Piece piece = PAWN;
        int idx = 0;
        char ch = input[0];
        if (ch == 'N') { piece = KNIGHT; idx++; }
        else if (ch == 'B') { piece = BISHOP; idx++; }
        else if (ch == 'R') { piece = ROOK; idx++; }
        else if (ch == 'Q') { piece = QUEEN; idx++; }
        else if (ch == 'K') { piece = KING; idx++; }

        // Skip 'x' for captures
        int disambig_col = -1, disambig_row = -1;
        // Check for disambiguation
        const char* rest = input + idx;
        int rlen = neo_strlen(rest);

        // Remove 'x'
        char clean[16];
        int ci = 0;
        for (int i = 0; i < rlen && ci < 15; i++) {
            if (rest[i] != 'x' && rest[i] != '+' && rest[i] != '#')
                clean[ci++] = rest[i];
        }
        clean[ci] = 0;
        int clen = neo_strlen(clean);

        int tc = -1, tr = -1;
        if (clen >= 2) {
            tc = clean[clen-2] - 'a';
            tr = '8' - clean[clen-1];
        }
        if (clen == 3) {
            // Disambiguation
            if (clean[0] >= 'a' && clean[0] <= 'h') disambig_col = clean[0] - 'a';
            else if (clean[0] >= '1' && clean[0] <= '8') disambig_row = '8' - clean[0];
        }

        if (tc >= 0 && tc < 8 && tr >= 0 && tr < 8) {
            for (int i = 0; i < count; i++) {
                if (moves[i].to_r == tr && moves[i].to_c == tc) {
                    if (b.sq[moves[i].from_r][moves[i].from_c].piece != piece) continue;
                    if (disambig_col >= 0 && moves[i].from_c != disambig_col) continue;
                    if (disambig_row >= 0 && moves[i].from_r != disambig_row) continue;
                    result = moves[i];
                    return true;
                }
            }
        }
    }

    return false;
}

// Drawing
const int BOARD_X = 4;
const int BOARD_Y = 2;

void draw_board() {
    neo::display::set_cursor(BOARD_X, BOARD_Y - 1);
    neo::display::set_fg(8);
    neo::display::printf("  a  b  c  d  e  f  g  h");

    for (int r = 0; r < 8; r++) {
        neo::display::set_cursor(BOARD_X - 2, BOARD_Y + r);
        neo::display::set_fg(8);
        neo::display::putchar('8' - r);
        neo::display::putchar(' ');

        for (int c = 0; c < 8; c++) {
            bool dark_sq = ((r + c) % 2 == 1);
            if (dark_sq) neo::display::set_bg(2); // dark green
            else neo::display::set_bg(7); // light

            Square& sq = g.board.sq[r][c];
            if (sq.occupied) {
                if (sq.side == WHITE) neo::display::set_fg(15); // bright white
                else neo::display::set_fg(0); // black
                char pc = piece_display(sq.piece, sq.side);
                neo::display::putchar(' ');
                neo::display::putchar(pc);
                neo::display::putchar(' ');
            } else {
                neo::display::printf("   ");
            }
        }
        neo::display::set_bg(0);
        neo::display::set_fg(8);
        neo::display::printf(" %c", '8' - r);
    }

    neo::display::set_cursor(BOARD_X, BOARD_Y + 8);
    neo::display::set_fg(8);
    neo::display::printf("  a  b  c  d  e  f  g  h");
    neo::display::set_fg(7);
}

void draw_info_panel() {
    int x = BOARD_X + 28;
    neo::display::set_cursor(x, 1);
    neo::display::set_fg(11);
    neo::display::set_bold(true);
    neo::display::printf("NeoChess");
    neo::display::set_bold(false);
    neo::display::set_fg(7);

    neo::display::set_cursor(x, 3);
    neo::display::printf("Turn: %s  ", g.board.to_move == WHITE ? "White" : "Black");
    neo::display::set_cursor(x, 4);
    neo::display::printf("Move: %d  ", g.board.fullmove);
    neo::display::set_cursor(x, 5);
    neo::display::printf("Depth: %d  ", g.ai_depth);

    if (g.game_over) {
        neo::display::set_cursor(x, 7);
        neo::display::set_fg(10);
        neo::display::printf("%s", g.status_msg);
        neo::display::set_fg(7);
    }

    // Move history (last few moves)
    neo::display::set_cursor(x, 9);
    neo::display::set_fg(14);
    neo::display::printf("History:");
    neo::display::set_fg(7);
    int start = g.history_count - 8;
    if (start < 0) start = 0;
    for (int i = start; i < g.history_count; i++) {
        char buf[16];
        move_to_str(g.history[i].move, buf);
        neo::display::set_cursor(x, 10 + i - start);
        neo::display::printf("%d. %s  ", i + 1, buf);
    }
}

void draw_help_bar() {
    int y = BOARD_Y + 10;
    neo::display::set_cursor(0, y);
    neo::display::set_fg(8);
    neo::display::printf("  Enter move (e.g. e2e4, Nf3, O-O) | U=Undo | D=Depth(1-4) | N=New | Q=Quit");
    neo::display::set_fg(7);
}

void draw_input() {
    int y = BOARD_Y + 12;
    neo::display::set_cursor(0, y);
    neo::display::clear_eol();
    neo::display::set_fg(14);
    neo::display::printf("  > ");
    neo::display::set_fg(7);
    for (int i = 0; i < g.input_len; i++) {
        neo::display::putchar(g.input_buf[i]);
    }
    neo::display::putchar('_');
}

void draw_status() {
    int y = BOARD_Y + 14;
    neo::display::set_cursor(0, y);
    neo::display::clear_eol();
    if (g.status_msg[0]) {
        neo::display::set_fg(9);
        neo::display::printf("  %s", g.status_msg);
        neo::display::set_fg(7);
    }
}

void full_draw() {
    neo::display::clear();
    draw_board();
    draw_info_panel();
    draw_help_bar();
    draw_input();
    draw_status();
}

void do_ai_move() {
    neo_strcpy(g.status_msg, "Thinking...");
    draw_status();

    Move m = ai_find_move(g.board);

    // Record
    g.history[g.history_count].move = m;
    g.history[g.history_count].board_before = g.board;
    g.history_count++;

    make_move(g.board, m);

    char buf[16];
    move_to_str(m, buf);
    ksprintf(g.status_msg, sizeof(g.status_msg), "AI plays: %s", buf);

    // Check for game end
    Move legal[MAX_MOVES];
    int lc = generate_legal_moves(g.board, legal);
    if (lc == 0) {
        if (in_check(g.board, g.board.to_move)) {
            neo_strcat(g.status_msg, " Checkmate!");
            g.game_over = true;
        } else {
            neo_strcat(g.status_msg, " Stalemate!");
            g.game_over = true;
        }
    } else if (in_check(g.board, g.board.to_move)) {
        neo_strcat(g.status_msg, " Check!");
    }
}

void undo_move() {
    if (g.history_count < 2) return; // Undo both player and AI move
    g.history_count -= 2;
    g.board = g.history[g.history_count].board_before;
    g.game_over = false;
    neo_strcpy(g.status_msg, "Move undone.");
}

void new_game() {
    init_board(g.board);
    g.history_count = 0;
    g.game_over = false;
    g.status_msg[0] = 0;
    g.input_len = 0;
    g.input_buf[0] = 0;

    if (!g.player_is_white) {
        do_ai_move();
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    g.ai_depth = 3;
    g.player_is_white = true;
    g.show_thinking = false;
    g.input_len = 0;
    g.input_buf[0] = 0;
    g.status_msg[0] = 0;

    new_game();
    full_draw();

    bool running = true;
    while (running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            bool shift = neo::keyboard::is_shift_down();
            char ch = neo::keyboard::translate(sc, shift);

            if (ch == 'q' || ch == 'Q') {
                if (g.input_len == 0) { running = false; continue; }
            }

            if (sc == 0x44) { // Return
                g.input_buf[g.input_len] = 0;

                if (g.input_len > 0 && !g.game_over) {
                    Move m;
                    if (parse_and_find_move(g.board, g.input_buf, m)) {
                        g.history[g.history_count].move = m;
                        g.history[g.history_count].board_before = g.board;
                        g.history_count++;
                        make_move(g.board, m);
                        g.status_msg[0] = 0;

                        // Check game end
                        Move legal[MAX_MOVES];
                        int lc = generate_legal_moves(g.board, legal);
                        if (lc == 0) {
                            if (in_check(g.board, g.board.to_move)) {
                                neo_strcpy(g.status_msg, "Checkmate! You win!");
                                g.game_over = true;
                            } else {
                                neo_strcpy(g.status_msg, "Stalemate!");
                                g.game_over = true;
                            }
                        } else {
                            // AI response
                            if (!g.game_over) {
                                full_draw();
                                do_ai_move();
                            }
                        }
                    } else {
                        neo_strcpy(g.status_msg, "Invalid move. Try e2e4, Nf3, O-O");
                    }
                }
                g.input_len = 0;
                full_draw();
            }
            else if (sc == 0x41) { // Backspace
                if (g.input_len > 0) g.input_len--;
                draw_input();
            }
            else if (ch == 'u' || ch == 'U') {
                if (g.input_len == 0) {
                    undo_move();
                    full_draw();
                } else {
                    if (g.input_len < 30) g.input_buf[g.input_len++] = ch;
                    draw_input();
                }
            }
            else if (ch == 'n' || ch == 'N') {
                if (g.input_len == 0) {
                    new_game();
                    full_draw();
                } else {
                    if (g.input_len < 30) g.input_buf[g.input_len++] = ch;
                    draw_input();
                }
            }
            else if (ch == 'd' || ch == 'D') {
                if (g.input_len == 0) {
                    g.ai_depth = (g.ai_depth % 4) + 1;
                    ksprintf(g.status_msg, sizeof(g.status_msg), "AI depth: %d", g.ai_depth);
                    full_draw();
                } else {
                    if (g.input_len < 30) g.input_buf[g.input_len++] = ch;
                    draw_input();
                }
            }
            else if (ch >= 32 && ch < 127) {
                if (g.input_len < 30) {
                    g.input_buf[g.input_len++] = ch;
                }
                draw_input();
            }
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    kprintf("Thanks for playing NeoChess!\n");
}
