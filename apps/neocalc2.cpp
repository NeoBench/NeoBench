#include "../include/neobench.h"
#include "../lib/string.h"

// NeoCalc2 - Desktop Calculator with Scientific Mode
// Standard: arithmetic with operator precedence
// Scientific: trig, sqrt, pow, log, ln, exp, factorial, pi, e
// Memory store/recall/clear, expression history, hex/oct/bin display

namespace {

// ---- Constants ----
constexpr int MAX_EXPR = 128;
constexpr int MAX_HIST = 20;
constexpr int STACK_MAX = 64;
constexpr double PI_VAL = 3.14159265358979323846;
constexpr double E_VAL  = 2.71828182845904523536;

// ---- Display modes ----
enum DisplayMode { DM_DEC, DM_HEX, DM_OCT, DM_BIN };
enum CalcMode { CM_STANDARD, CM_SCIENTIFIC };

// ---- History entry ----
struct HistEntry {
    char expr[MAX_EXPR];
    char result[64];
};

// ---- Calculator state ----
struct CalcState {
    char expr[MAX_EXPR];
    int expr_len;
    char display[64];
    double memory;
    bool memory_set;
    double last_result;
    bool has_result;
    DisplayMode disp_mode;
    CalcMode calc_mode;
    HistEntry history[MAX_HIST];
    int hist_count;
    int hist_scroll;
    bool running;
    bool error;
    char error_msg[64];
};

static CalcState calc;

// ---- Math helpers ----
static double neo_fabs(double x) { return x < 0 ? -x : x; }

static double neo_fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    int q = (int)(x / y);
    return x - q * y;
}

static double neo_sqrt(double x) {
    if (x < 0) return -1.0;
    if (x == 0) return 0;
    double g = x / 2.0;
    for (int i = 0; i < 50; i++) {
        g = (g + x / g) / 2.0;
    }
    return g;
}

static double neo_pow(double base, double exp_val) {
    if (exp_val == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    // integer exponent
    int ie = (int)exp_val;
    if ((double)ie == exp_val && ie >= 0) {
        double r = 1.0;
        double b = base;
        int e = ie;
        while (e > 0) {
            if (e & 1) r *= b;
            b *= b;
            e >>= 1;
        }
        return r;
    }
    // Use exp(exp_val * ln(base))
    // Fallback for non-integer: Taylor series
    double ln_base = 0.0;
    // ln via series: ln(x) = 2*sum( ((x-1)/(x+1))^(2k+1) / (2k+1) )
    double ratio = (base - 1.0) / (base + 1.0);
    double term = ratio;
    for (int k = 0; k < 60; k++) {
        ln_base += term / (2 * k + 1);
        term *= ratio * ratio;
    }
    ln_base *= 2.0;
    double y = exp_val * ln_base;
    // exp via Taylor
    double result = 1.0;
    double t = 1.0;
    for (int k = 1; k < 40; k++) {
        t *= y / k;
        result += t;
    }
    return result;
}

static double neo_ln(double x) {
    if (x <= 0) return -1e30;
    // Normalize: x = f * 2^n where 0.5 <= f < 1
    double ratio = (x - 1.0) / (x + 1.0);
    double result = 0.0;
    double term = ratio;
    for (int k = 0; k < 80; k++) {
        result += term / (2 * k + 1);
        term *= ratio * ratio;
    }
    return 2.0 * result;
}

static double neo_log10(double x) { return neo_ln(x) / neo_ln(10.0); }

static double neo_exp(double x) {
    double result = 1.0, term = 1.0;
    for (int k = 1; k < 40; k++) {
        term *= x / k;
        result += term;
    }
    return result;
}

static double neo_sin(double x) {
    x = neo_fmod(x, 2.0 * PI_VAL);
    double result = 0.0, term = x;
    for (int k = 0; k < 20; k++) {
        result += term;
        term *= -x * x / ((2 * k + 2) * (2 * k + 3));
    }
    return result;
}

static double neo_cos(double x) { return neo_sin(x + PI_VAL / 2.0); }
static double neo_tan(double x) { return neo_sin(x) / neo_cos(x); }

static double neo_factorial(int n) {
    if (n < 0) return -1;
    double r = 1.0;
    for (int i = 2; i <= n; i++) r *= i;
    return r;
}

// ---- Tokenizer & Expression Evaluator (recursive descent) ----
struct Token {
    enum Type { NUM, OP, LPAREN, RPAREN, FUNC, END } type;
    double num_val;
    char op;
    char func_name[16];
};

struct Parser {
    const char* src;
    int pos;
    bool error;

    void skip_spaces() {
        while (src[pos] == ' ') pos++;
    }

    Token next_token() {
        skip_spaces();
        Token t;
        t.num_val = 0;
        t.op = 0;
        t.func_name[0] = 0;

        char c = src[pos];
        if (c == 0) { t.type = Token::END; return t; }

        if (c == '(') { t.type = Token::LPAREN; pos++; return t; }
        if (c == ')') { t.type = Token::RPAREN; pos++; return t; }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
            t.type = Token::OP; t.op = c; pos++; return t;
        }

        // Number
        if ((c >= '0' && c <= '9') || c == '.') {
            t.type = Token::NUM;
            double val = 0; bool frac = false; double div = 10.0;
            while ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.') {
                if (src[pos] == '.') { frac = true; pos++; continue; }
                if (frac) { val += (src[pos] - '0') / div; div *= 10.0; }
                else { val = val * 10 + (src[pos] - '0'); }
                pos++;
            }
            t.num_val = val;
            return t;
        }

        // Function name or constant
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            t.type = Token::FUNC;
            int i = 0;
            while (((src[pos] >= 'a' && src[pos] <= 'z') ||
                    (src[pos] >= 'A' && src[pos] <= 'Z') ||
                    (src[pos] >= '0' && src[pos] <= '9')) && i < 15) {
                t.func_name[i++] = src[pos++];
            }
            t.func_name[i] = 0;
            // Check constants
            if (neo_strcmp(t.func_name, "pi") == 0 || neo_strcmp(t.func_name, "PI") == 0) {
                t.type = Token::NUM; t.num_val = PI_VAL;
            } else if (neo_strcmp(t.func_name, "e") == 0 || neo_strcmp(t.func_name, "E") == 0) {
                t.type = Token::NUM; t.num_val = E_VAL;
            }
            return t;
        }

        error = true;
        t.type = Token::END;
        return t;
    }

    Token peek() {
        int saved = pos;
        Token t = next_token();
        pos = saved;
        return t;
    }

    // expr = term (('+' | '-') term)*
    double parse_expr() {
        double val = parse_term();
        while (true) {
            Token t = peek();
            if (t.type == Token::OP && (t.op == '+' || t.op == '-')) {
                next_token();
                double rhs = parse_term();
                if (t.op == '+') val += rhs; else val -= rhs;
            } else break;
        }
        return val;
    }

    // term = power (('*' | '/' | '%') power)*
    double parse_term() {
        double val = parse_power();
        while (true) {
            Token t = peek();
            if (t.type == Token::OP && (t.op == '*' || t.op == '/' || t.op == '%')) {
                next_token();
                double rhs = parse_power();
                if (t.op == '*') val *= rhs;
                else if (t.op == '/') { if (rhs == 0) { error = true; return 0; } val /= rhs; }
                else { val = neo_fmod(val, rhs); }
            } else break;
        }
        return val;
    }

    // power = unary ('^' power)?
    double parse_power() {
        double val = parse_unary();
        Token t = peek();
        if (t.type == Token::OP && t.op == '^') {
            next_token();
            double rhs = parse_power();
            val = neo_pow(val, rhs);
        }
        return val;
    }

    // unary = '-' unary | '+' unary | primary
    double parse_unary() {
        Token t = peek();
        if (t.type == Token::OP && t.op == '-') {
            next_token();
            return -parse_unary();
        }
        if (t.type == Token::OP && t.op == '+') {
            next_token();
            return parse_unary();
        }
        return parse_primary();
    }

    // primary = NUM | '(' expr ')' | func '(' expr ')' | func primary
    double parse_primary() {
        Token t = next_token();
        if (t.type == Token::NUM) return t.num_val;
        if (t.type == Token::LPAREN) {
            double val = parse_expr();
            Token closing = next_token();
            if (closing.type != Token::RPAREN) error = true;
            return val;
        }
        if (t.type == Token::FUNC) {
            // Expect '(' expr ')'
            Token lp = peek();
            double arg = 0;
            if (lp.type == Token::LPAREN) {
                next_token();
                arg = parse_expr();
                Token rp = next_token();
                if (rp.type != Token::RPAREN) error = true;
            } else {
                arg = parse_unary();
            }
            if (neo_strcmp(t.func_name, "sin") == 0) return neo_sin(arg);
            if (neo_strcmp(t.func_name, "cos") == 0) return neo_cos(arg);
            if (neo_strcmp(t.func_name, "tan") == 0) return neo_tan(arg);
            if (neo_strcmp(t.func_name, "sqrt") == 0) return neo_sqrt(arg);
            if (neo_strcmp(t.func_name, "log") == 0) return neo_log10(arg);
            if (neo_strcmp(t.func_name, "ln") == 0) return neo_ln(arg);
            if (neo_strcmp(t.func_name, "exp") == 0) return neo_exp(arg);
            if (neo_strcmp(t.func_name, "abs") == 0) return neo_fabs(arg);
            if (neo_strcmp(t.func_name, "fact") == 0) return neo_factorial((int)arg);
            error = true;
            return 0;
        }
        error = true;
        return 0;
    }
};

// ---- Formatting ----
static void format_double(char* buf, int bufsize, double val) {
    if (val != val) { neo_strcpy(buf, "NaN"); return; } // NaN check
    if (val < -1e15 || val > 1e15) { neo_strcpy(buf, "Overflow"); return; }

    bool neg = val < 0;
    if (neg) val = -val;

    long long integer = (long long)val;
    double frac = val - (double)integer;

    char tmp[64];
    int pos = 0;

    if (neg) tmp[pos++] = '-';

    // Integer part
    char ibuf[32];
    int ilen = 0;
    if (integer == 0) { ibuf[ilen++] = '0'; }
    else {
        long long t = integer;
        while (t > 0 && ilen < 30) { ibuf[ilen++] = '0' + (int)(t % 10); t /= 10; }
    }
    for (int i = ilen - 1; i >= 0; i--) tmp[pos++] = ibuf[i];

    // Fractional part (up to 8 digits, strip trailing zeros)
    if (frac > 0.000000005) {
        tmp[pos++] = '.';
        char fbuf[12];
        int flen = 0;
        for (int i = 0; i < 8; i++) {
            frac *= 10.0;
            int d = (int)frac;
            if (d > 9) d = 9;
            fbuf[flen++] = '0' + d;
            frac -= d;
        }
        // Strip trailing zeros
        while (flen > 1 && fbuf[flen - 1] == '0') flen--;
        for (int i = 0; i < flen; i++) tmp[pos++] = fbuf[i];
    }
    tmp[pos] = 0;
    neo_strncpy(buf, tmp, bufsize - 1);
    buf[bufsize - 1] = 0;
}

static void format_hex(char* buf, int bufsize, long long val) {
    const char* hex = "0123456789ABCDEF";
    bool neg = val < 0;
    if (neg) val = -val;
    char tmp[32];
    int pos = 0;
    if (val == 0) { tmp[pos++] = '0'; }
    else { while (val > 0 && pos < 16) { tmp[pos++] = hex[val & 0xF]; val >>= 4; } }
    int out = 0;
    if (neg) buf[out++] = '-';
    buf[out++] = '0'; buf[out++] = 'x';
    for (int i = pos - 1; i >= 0 && out < bufsize - 1; i--) buf[out++] = tmp[i];
    buf[out] = 0;
}

static void format_oct(char* buf, int bufsize, long long val) {
    bool neg = val < 0;
    if (neg) val = -val;
    char tmp[32];
    int pos = 0;
    if (val == 0) { tmp[pos++] = '0'; }
    else { while (val > 0 && pos < 22) { tmp[pos++] = '0' + (int)(val & 7); val >>= 3; } }
    int out = 0;
    if (neg) buf[out++] = '-';
    buf[out++] = '0'; buf[out++] = 'o';
    for (int i = pos - 1; i >= 0 && out < bufsize - 1; i--) buf[out++] = tmp[i];
    buf[out] = 0;
}

static void format_bin(char* buf, int bufsize, long long val) {
    bool neg = val < 0;
    if (neg) val = -val;
    char tmp[64];
    int pos = 0;
    if (val == 0) { tmp[pos++] = '0'; }
    else { while (val > 0 && pos < 32) { tmp[pos++] = '0' + (int)(val & 1); val >>= 1; } }
    int out = 0;
    if (neg) buf[out++] = '-';
    buf[out++] = '0'; buf[out++] = 'b';
    for (int i = pos - 1; i >= 0 && out < bufsize - 1; i--) buf[out++] = tmp[i];
    buf[out] = 0;
}

// ---- UI Drawing ----
static void draw_box(int x, int y, int w, int h) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    for (int r = 1; r < h - 1; r++) {
        neo::display::set_cursor(x, y + r);
        neo::display::putchar('|');
        for (int i = 0; i < w - 2; i++) neo::display::putchar(' ');
        neo::display::putchar('|');
    }

    neo::display::set_cursor(x, y + h - 1);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');
}

static void draw_text(int x, int y, const char* text) {
    neo::display::set_cursor(x, y);
    neo::display::puts(text);
}

static void evaluate_expression() {
    if (calc.expr_len == 0) return;

    Parser parser;
    parser.src = calc.expr;
    parser.pos = 0;
    parser.error = false;

    double result = parser.parse_expr();

    if (parser.error) {
        calc.error = true;
        neo_strcpy(calc.error_msg, "Syntax Error");
        return;
    }

    calc.last_result = result;
    calc.has_result = true;
    calc.error = false;

    // Format result based on display mode
    switch (calc.disp_mode) {
        case DM_DEC: format_double(calc.display, 64, result); break;
        case DM_HEX: format_hex(calc.display, 64, (long long)result); break;
        case DM_OCT: format_oct(calc.display, 64, (long long)result); break;
        case DM_BIN: format_bin(calc.display, 64, (long long)result); break;
    }

    // Add to history
    if (calc.hist_count < MAX_HIST) {
        neo_strcpy(calc.history[calc.hist_count].expr, calc.expr);
        neo_strcpy(calc.history[calc.hist_count].result, calc.display);
        calc.hist_count++;
    } else {
        for (int i = 0; i < MAX_HIST - 1; i++) {
            neo_memcpy(&calc.history[i], &calc.history[i + 1], sizeof(HistEntry));
        }
        neo_strcpy(calc.history[MAX_HIST - 1].expr, calc.expr);
        neo_strcpy(calc.history[MAX_HIST - 1].result, calc.display);
    }
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();

    // Title bar
    neo::display::set_bold(true);
    neo::display::set_color(15, 1);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::puts("NeoCalc2");
    neo::display::set_cursor(w - 20, 0);
    neo::display::puts(calc.calc_mode == CM_STANDARD ? "[Standard]" : "[Scientific]");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    // Display mode indicator
    const char* mode_str = "DEC";
    if (calc.disp_mode == DM_HEX) mode_str = "HEX";
    else if (calc.disp_mode == DM_OCT) mode_str = "OCT";
    else if (calc.disp_mode == DM_BIN) mode_str = "BIN";
    draw_text(w - 6, 2, mode_str);

    // Expression box
    draw_box(1, 2, w - 8, 3);
    neo::display::set_cursor(3, 3);
    int max_disp = w - 12;
    int start = 0;
    if (calc.expr_len > max_disp) start = calc.expr_len - max_disp;
    for (int i = start; i < calc.expr_len; i++) {
        neo::display::putchar(calc.expr[i]);
    }
    neo::display::putchar('_');

    // Result box
    draw_box(1, 5, w - 8, 3);
    if (calc.error) {
        neo::display::set_color(12, 0);
        draw_text(3, 6, calc.error_msg);
        neo::display::set_color(7, 0);
    } else if (calc.has_result) {
        neo::display::set_bold(true);
        neo::display::set_color(10, 0);
        draw_text(3, 6, "= ");
        neo::display::puts(calc.display);
        neo::display::set_bold(false);
        neo::display::set_color(7, 0);
    }

    // Memory indicator
    if (calc.memory_set) {
        neo::display::set_color(14, 0);
        draw_text(3, 8, "M:");
        char mbuf[32];
        format_double(mbuf, 32, calc.memory);
        neo::display::puts(mbuf);
        neo::display::set_color(7, 0);
    }

    // Function buttons display (scientific mode)
    int row = 9;
    if (calc.calc_mode == CM_SCIENTIFIC) {
        draw_text(2, row, "Functions: sin cos tan sqrt log ln exp abs fact");
        row++;
        draw_text(2, row, "Constants: pi e");
        row++;
    }

    // History
    row++;
    neo::display::set_bold(true);
    draw_text(2, row, "-- History --");
    neo::display::set_bold(false);
    row++;

    int h = neo::display::get_height();
    int avail = h - row - 3;
    int start_idx = calc.hist_count > avail ? calc.hist_count - avail : 0;
    for (int i = start_idx; i < calc.hist_count && row < h - 3; i++) {
        neo::display::set_color(8, 0);
        neo::display::set_cursor(3, row);
        neo::display::puts(calc.history[i].expr);
        neo::display::set_color(10, 0);
        neo::display::puts(" = ");
        neo::display::puts(calc.history[i].result);
        neo::display::set_color(7, 0);
        row++;
    }

    // Status bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("Enter=Eval  Tab=Mode  F1=Sci/Std  F2=Hex F3=Oct F4=Bin F5=Dec  MS/MR/MC  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(sc, shift);

    // ESC
    if (sc == 0x01) { calc.running = false; return; }

    // Enter - evaluate
    if (sc == 0x44 || ch == '\r' || ch == '\n') {
        evaluate_expression();
        draw_ui();
        return;
    }

    // Backspace
    if (sc == 0x41 || ch == 8) {
        if (calc.expr_len > 0) {
            calc.expr[--calc.expr_len] = 0;
            calc.error = false;
        }
        draw_ui();
        return;
    }

    // Tab - toggle standard/scientific
    if (sc == 0x42) {
        calc.calc_mode = (calc.calc_mode == CM_STANDARD) ? CM_SCIENTIFIC : CM_STANDARD;
        draw_ui();
        return;
    }

    // F1 - toggle mode
    if (sc == 0x50) {
        calc.calc_mode = (calc.calc_mode == CM_STANDARD) ? CM_SCIENTIFIC : CM_STANDARD;
        draw_ui();
        return;
    }

    // F2-F5 display modes
    if (sc == 0x51) { calc.disp_mode = DM_HEX; if (calc.has_result) { format_hex(calc.display, 64, (long long)calc.last_result); } draw_ui(); return; }
    if (sc == 0x52) { calc.disp_mode = DM_OCT; if (calc.has_result) { format_oct(calc.display, 64, (long long)calc.last_result); } draw_ui(); return; }
    if (sc == 0x53) { calc.disp_mode = DM_BIN; if (calc.has_result) { format_bin(calc.display, 64, (long long)calc.last_result); } draw_ui(); return; }
    if (sc == 0x54) { calc.disp_mode = DM_DEC; if (calc.has_result) { format_double(calc.display, 64, calc.last_result); } draw_ui(); return; }

    // Printable characters
    if (ch >= 32 && ch < 127 && calc.expr_len < MAX_EXPR - 1) {
        // Memory shortcuts: S=store, R=recall, C=clear (with Ctrl-like behavior)
        // Use F6=MS, F7=MR, F8=MC instead for simplicity
        calc.expr[calc.expr_len++] = ch;
        calc.expr[calc.expr_len] = 0;
        calc.error = false;
        draw_ui();
        return;
    }

    // F6 = Memory Store
    if (sc == 0x55) {
        if (calc.has_result) { calc.memory = calc.last_result; calc.memory_set = true; }
        draw_ui();
        return;
    }
    // F7 = Memory Recall
    if (sc == 0x56) {
        if (calc.memory_set) {
            char mbuf[32];
            format_double(mbuf, 32, calc.memory);
            int len = neo_strlen(mbuf);
            for (int i = 0; i < len && calc.expr_len < MAX_EXPR - 1; i++) {
                calc.expr[calc.expr_len++] = mbuf[i];
            }
            calc.expr[calc.expr_len] = 0;
        }
        draw_ui();
        return;
    }
    // F8 = Memory Clear
    if (sc == 0x57) {
        calc.memory = 0;
        calc.memory_set = false;
        draw_ui();
        return;
    }

    // Delete / clear expression
    if (sc == 0x46) { // Del key
        calc.expr_len = 0;
        calc.expr[0] = 0;
        calc.error = false;
        calc.has_result = false;
        draw_ui();
        return;
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&calc, 0, sizeof(calc));
    calc.running = true;
    calc.disp_mode = DM_DEC;
    calc.calc_mode = CM_STANDARD;

    draw_ui();

    while (calc.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
        }
        neo::timer::delay_ms(10);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("NeoCalc2 exited.\n");
}
