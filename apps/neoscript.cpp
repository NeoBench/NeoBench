#include "../include/neobench.h"
#include "../lib/string.h"

// NeoScript - Simple Scripting Language Interpreter
// Variables, arithmetic, strings, if/else, while, print, functions, file I/O

namespace neoscript {

static const int MAX_VARS = 64;
static const int MAX_FUNCS = 32;
static const int MAX_LINES = 512;
static const int MAX_LINE_LEN = INODE_SIZE;
static const int MAX_CALL_DEPTH = 16;
static const int MAX_STR_LEN = INODE_SIZE;

// --- Token types ---
enum TokenType {
    TOK_EOF = 0, TOK_NUM, TOK_STR, TOK_IDENT,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_MOD,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_ASSIGN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_COMMA, TOK_SEMICOLON, TOK_DOT,
    // Keywords
    TOK_VAR, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR,
    TOK_FUNC, TOK_RETURN, TOK_PRINT, TOK_PRINTLN,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_FOPEN, TOK_FREAD, TOK_FWRITE, TOK_FCLOSE,
    TOK_LEN, TOK_SUBSTR, TOK_INPUT,
};

struct Token {
    TokenType type;
    long num_val;
    char str_val[MAX_STR_LEN];
};

// --- Value type ---
enum ValueType { VAL_NUM, VAL_STR };

struct Value {
    ValueType type;
    long num;
    char str[MAX_STR_LEN];
};

// --- Variable ---
struct Variable {
    char name[32];
    Value val;
    bool used;
};

// --- Function ---
struct Function {
    char name[32];
    char params[8][32];
    int param_count;
    int start_line;
    int end_line;
    bool defined;
};

// --- Script state ---
static char source_lines[MAX_LINES][MAX_LINE_LEN];
static int line_count = 0;
static int current_line = 0;
static int line_pos = 0;
static Variable vars[MAX_VARS];
static Function funcs[MAX_FUNCS];
static int func_count = 0;
static bool had_error = false;
static char error_msg[128];
static int error_line = 0;

// Call stack
struct CallFrame {
    int return_line;
    Variable locals[8];
    int local_count;
};
static CallFrame call_stack[MAX_CALL_DEPTH];
static int call_depth = 0;

// --- Error reporting ---
static void report_error(const char* msg) {
    had_error = true;
    neo_strncpy(error_msg, msg, 127);
    error_msg[127] = 0;
    error_line = current_line + 1;
    neo::display::set_fg(12);
    neo::display::printf("Error (line %d): %s\n", error_line, msg);
    neo::display::set_fg(7);
}

// --- Lexer ---
static const char* line_src = nullptr;

static void skip_whitespace() {
    while (line_src[line_pos] == ' ' || line_src[line_pos] == '\t')
        line_pos++;
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static Token next_token() {
    Token tok;
    tok.type = TOK_EOF;
    tok.num_val = 0;
    tok.str_val[0] = 0;

    skip_whitespace();
    char c = line_src[line_pos];
    if (c == 0 || c == '\n' || c == '#') { tok.type = TOK_EOF; return tok; }

    // Number
    if (is_digit(c) || (c == '-' && is_digit(line_src[line_pos+1]))) {
        bool neg = false;
        if (c == '-') { neg = true; line_pos++; }
        long val = 0;
        while (is_digit(line_src[line_pos])) {
            val = val * 10 + (line_src[line_pos] - '0');
            line_pos++;
        }
        tok.type = TOK_NUM;
        tok.num_val = neg ? -val : val;
        return tok;
    }

    // String literal
    if (c == '"') {
        line_pos++;
        int si = 0;
        while (line_src[line_pos] && line_src[line_pos] != '"' && si < MAX_STR_LEN - 1) {
            if (line_src[line_pos] == '\\' && line_src[line_pos+1]) {
                line_pos++;
                switch (line_src[line_pos]) {
                    case 'n': tok.str_val[si++] = '\n'; break;
                    case 't': tok.str_val[si++] = '\t'; break;
                    case '\\': tok.str_val[si++] = '\\'; break;
                    case '"': tok.str_val[si++] = '"'; break;
                    default: tok.str_val[si++] = line_src[line_pos]; break;
                }
            } else {
                tok.str_val[si++] = line_src[line_pos];
            }
            line_pos++;
        }
        tok.str_val[si] = 0;
        if (line_src[line_pos] == '"') line_pos++;
        tok.type = TOK_STR;
        return tok;
    }

    // Identifier or keyword
    if (is_alpha(c)) {
        int si = 0;
        while (is_alnum(line_src[line_pos]) && si < MAX_STR_LEN - 1) {
            tok.str_val[si++] = line_src[line_pos++];
        }
        tok.str_val[si] = 0;

        // Keywords
        if (neo_strcmp(tok.str_val, "var") == 0) tok.type = TOK_VAR;
        else if (neo_strcmp(tok.str_val, "if") == 0) tok.type = TOK_IF;
        else if (neo_strcmp(tok.str_val, "else") == 0) tok.type = TOK_ELSE;
        else if (neo_strcmp(tok.str_val, "while") == 0) tok.type = TOK_WHILE;
        else if (neo_strcmp(tok.str_val, "for") == 0) tok.type = TOK_FOR;
        else if (neo_strcmp(tok.str_val, "func") == 0) tok.type = TOK_FUNC;
        else if (neo_strcmp(tok.str_val, "return") == 0) tok.type = TOK_RETURN;
        else if (neo_strcmp(tok.str_val, "print") == 0) tok.type = TOK_PRINT;
        else if (neo_strcmp(tok.str_val, "println") == 0) tok.type = TOK_PRINTLN;
        else if (neo_strcmp(tok.str_val, "and") == 0) tok.type = TOK_AND;
        else if (neo_strcmp(tok.str_val, "or") == 0) tok.type = TOK_OR;
        else if (neo_strcmp(tok.str_val, "not") == 0) tok.type = TOK_NOT;
        else if (neo_strcmp(tok.str_val, "fopen") == 0) tok.type = TOK_FOPEN;
        else if (neo_strcmp(tok.str_val, "fread") == 0) tok.type = TOK_FREAD;
        else if (neo_strcmp(tok.str_val, "fwrite") == 0) tok.type = TOK_FWRITE;
        else if (neo_strcmp(tok.str_val, "fclose") == 0) tok.type = TOK_FCLOSE;
        else if (neo_strcmp(tok.str_val, "len") == 0) tok.type = TOK_LEN;
        else if (neo_strcmp(tok.str_val, "substr") == 0) tok.type = TOK_SUBSTR;
        else if (neo_strcmp(tok.str_val, "input") == 0) tok.type = TOK_INPUT;
        else tok.type = TOK_IDENT;
        return tok;
    }

    // Operators
    line_pos++;
    switch (c) {
        case '+': tok.type = TOK_PLUS; break;
        case '-': tok.type = TOK_MINUS; break;
        case '*': tok.type = TOK_STAR; break;
        case '/': tok.type = TOK_SLASH; break;
        case '%': tok.type = TOK_MOD; break;
        case '(': tok.type = TOK_LPAREN; break;
        case ')': tok.type = TOK_RPAREN; break;
        case '{': tok.type = TOK_LBRACE; break;
        case '}': tok.type = TOK_RBRACE; break;
        case ',': tok.type = TOK_COMMA; break;
        case ';': tok.type = TOK_SEMICOLON; break;
        case '.': tok.type = TOK_DOT; break;
        case '=':
            if (line_src[line_pos] == '=') { line_pos++; tok.type = TOK_EQ; }
            else tok.type = TOK_ASSIGN;
            break;
        case '!':
            if (line_src[line_pos] == '=') { line_pos++; tok.type = TOK_NEQ; }
            else tok.type = TOK_NOT;
            break;
        case '<':
            if (line_src[line_pos] == '=') { line_pos++; tok.type = TOK_LTE; }
            else tok.type = TOK_LT;
            break;
        case '>':
            if (line_src[line_pos] == '=') { line_pos++; tok.type = TOK_GTE; }
            else tok.type = TOK_GT;
            break;
        default:
            report_error("Unexpected character");
            break;
    }
    return tok;
}

static Token peek_token() {
    int saved_pos = line_pos;
    Token t = next_token();
    line_pos = saved_pos;
    return t;
}

// --- Variables ---
static Variable* find_var(const char* name) {
    // Check locals first
    if (call_depth > 0) {
        CallFrame& frame = call_stack[call_depth - 1];
        for (int i = 0; i < frame.local_count; i++) {
            if (neo_strcmp(frame.locals[i].name, name) == 0)
                return &frame.locals[i];
        }
    }
    // Globals
    for (int i = 0; i < MAX_VARS; i++) {
        if (vars[i].used && neo_strcmp(vars[i].name, name) == 0)
            return &vars[i];
    }
    return nullptr;
}

static Variable* create_var(const char* name) {
    for (int i = 0; i < MAX_VARS; i++) {
        if (!vars[i].used) {
            vars[i].used = true;
            neo_strcpy(vars[i].name, name);
            vars[i].val.type = VAL_NUM;
            vars[i].val.num = 0;
            vars[i].val.str[0] = 0;
            return &vars[i];
        }
    }
    report_error("Too many variables");
    return nullptr;
}

// --- Functions ---
static Function* find_func(const char* name) {
    for (int i = 0; i < func_count; i++) {
        if (neo_strcmp(funcs[i].name, name) == 0)
            return &funcs[i];
    }
    return nullptr;
}

// --- Expression parser (recursive descent) ---
static Value parse_expr();

static Value make_num(long n) {
    Value v;
    v.type = VAL_NUM;
    v.num = n;
    v.str[0] = 0;
    return v;
}

static Value make_str(const char* s) {
    Value v;
    v.type = VAL_STR;
    v.num = 0;
    neo_strncpy(v.str, s, MAX_STR_LEN - 1);
    v.str[MAX_STR_LEN - 1] = 0;
    return v;
}

static Value parse_primary() {
    Token tok = next_token();

    if (tok.type == TOK_NUM) return make_num(tok.num_val);
    if (tok.type == TOK_STR) return make_str(tok.str_val);

    if (tok.type == TOK_LPAREN) {
        Value v = parse_expr();
        Token rp = next_token();
        if (rp.type != TOK_RPAREN) report_error("Expected ')'");
        return v;
    }

    if (tok.type == TOK_NOT) {
        Value v = parse_primary();
        return make_num(v.num == 0 ? 1 : 0);
    }

    if (tok.type == TOK_MINUS) {
        Value v = parse_primary();
        return make_num(-v.num);
    }

    if (tok.type == TOK_LEN) {
        Token lp = next_token();
        if (lp.type != TOK_LPAREN) { report_error("Expected '('"); return make_num(0); }
        Value v = parse_expr();
        Token rp = next_token();
        if (rp.type != TOK_RPAREN) report_error("Expected ')'");
        if (v.type == VAL_STR) return make_num(neo_strlen(v.str));
        return make_num(0);
    }

    if (tok.type == TOK_SUBSTR) {
        Token lp = next_token();
        if (lp.type != TOK_LPAREN) { report_error("Expected '('"); return make_str(""); }
        Value s = parse_expr();
        Token c1 = next_token(); // comma
        Value start = parse_expr();
        Token c2 = next_token(); // comma
        Value length = parse_expr();
        Token rp = next_token();
        (void)c1; (void)c2;
        if (rp.type != TOK_RPAREN) report_error("Expected ')'");

        char result[MAX_STR_LEN];
        int slen = neo_strlen(s.str);
        int si = (int)start.num;
        int ln = (int)length.num;
        if (si < 0) si = 0;
        if (si > slen) si = slen;
        if (ln > slen - si) ln = slen - si;
        if (ln < 0) ln = 0;
        neo_memcpy(result, s.str + si, ln);
        result[ln] = 0;
        return make_str(result);
    }

    if (tok.type == TOK_INPUT) {
        Token lp = next_token();
        Value prompt = make_str("");
        if (lp.type == TOK_LPAREN) {
            prompt = parse_expr();
            next_token();  // rparen
        }
        char buf[128];
        neo::console::getline(buf, sizeof(buf), prompt.str);
        return make_str(buf);
    }

    if (tok.type == TOK_IDENT) {
        // Check for function call
        Token peek = peek_token();
        if (peek.type == TOK_LPAREN) {
            Function* fn = find_func(tok.str_val);
            if (!fn) {
                report_error("Unknown function");
                return make_num(0);
            }
            next_token();  // consume (

            // Parse arguments
            Value args[8];
            int arg_count = 0;
            Token pt = peek_token();
            if (pt.type != TOK_RPAREN) {
                args[arg_count++] = parse_expr();
                while (peek_token().type == TOK_COMMA) {
                    next_token();  // consume comma
                    if (arg_count < 8) args[arg_count++] = parse_expr();
                }
            }
            next_token();  // consume )

            if (call_depth >= MAX_CALL_DEPTH) {
                report_error("Stack overflow");
                return make_num(0);
            }

            // Set up call frame
            CallFrame& frame = call_stack[call_depth];
            frame.return_line = current_line;
            frame.local_count = 0;

            for (int i = 0; i < fn->param_count && i < arg_count; i++) {
                neo_strcpy(frame.locals[frame.local_count].name, fn->params[i]);
                frame.locals[frame.local_count].val = args[i];
                frame.locals[frame.local_count].used = true;
                frame.local_count++;
            }

            call_depth++;

            // Execute function body
            int saved_line = current_line;
            Value ret = make_num(0);

            for (current_line = fn->start_line; current_line <= fn->end_line && !had_error; current_line++) {
                line_src = source_lines[current_line];
                line_pos = 0;
                skip_whitespace();
                if (line_src[line_pos] == 0 || line_src[line_pos] == '#') continue;
                if (line_src[line_pos] == '}') break;

                Token ft = peek_token();
                if (ft.type == TOK_RETURN) {
                    next_token();
                    if (peek_token().type != TOK_EOF)
                        ret = parse_expr();
                    break;
                }

                // Execute statement (simplified - just handle assignments and print)
                if (ft.type == TOK_PRINT || ft.type == TOK_PRINTLN) {
                    next_token();
                    Value v = parse_expr();
                    if (v.type == VAL_STR) neo::display::printf("%s", v.str);
                    else neo::display::printf("%ld", v.num);
                    if (ft.type == TOK_PRINTLN) neo::display::putchar('\n');
                } else if (ft.type == TOK_IDENT || ft.type == TOK_VAR) {
                    // Variable assignment
                    if (ft.type == TOK_VAR) next_token();
                    Token name = next_token();
                    Token eq = next_token();
                    if (eq.type == TOK_ASSIGN) {
                        Value val = parse_expr();
                        Variable* v = find_var(name.str_val);
                        if (!v) v = create_var(name.str_val);
                        if (v) v->val = val;
                    }
                }
            }

            call_depth--;
            current_line = saved_line;
            line_src = source_lines[current_line];
            return ret;
        }

        // Variable lookup
        Variable* v = find_var(tok.str_val);
        if (v) return v->val;

        // Undefined variable - create with 0
        return make_num(0);
    }

    return make_num(0);
}

static Value parse_term() {
    Value left = parse_primary();
    while (!had_error) {
        Token op = peek_token();
        if (op.type == TOK_STAR || op.type == TOK_SLASH || op.type == TOK_MOD) {
            next_token();
            Value right = parse_primary();
            if (op.type == TOK_STAR) left = make_num(left.num * right.num);
            else if (op.type == TOK_SLASH) {
                if (right.num == 0) { report_error("Division by zero"); return make_num(0); }
                left = make_num(left.num / right.num);
            } else left = make_num(left.num % right.num);
        } else break;
    }
    return left;
}

static Value parse_add() {
    Value left = parse_term();
    while (!had_error) {
        Token op = peek_token();
        if (op.type == TOK_PLUS) {
            next_token();
            Value right = parse_term();
            if (left.type == VAL_STR || right.type == VAL_STR) {
                // String concatenation
                char buf[MAX_STR_LEN];
                if (left.type == VAL_STR) neo_strcpy(buf, left.str);
                else ksprintf(buf, MAX_STR_LEN, "%ld", left.num);

                char rbuf[64];
                if (right.type == VAL_STR) neo_strcat(buf, right.str);
                else { ksprintf(rbuf, 64, "%ld", right.num); neo_strcat(buf, rbuf); }

                left = make_str(buf);
            } else {
                left = make_num(left.num + right.num);
            }
        } else if (op.type == TOK_MINUS) {
            next_token();
            Value right = parse_term();
            left = make_num(left.num - right.num);
        } else break;
    }
    return left;
}

static Value parse_comparison() {
    Value left = parse_add();
    Token op = peek_token();
    if (op.type == TOK_EQ || op.type == TOK_NEQ || op.type == TOK_LT ||
        op.type == TOK_GT || op.type == TOK_LTE || op.type == TOK_GTE) {
        next_token();
        Value right = parse_add();
        bool result = false;
        if (left.type == VAL_STR && right.type == VAL_STR) {
            int cmp = neo_strcmp(left.str, right.str);
            switch (op.type) {
                case TOK_EQ: result = (cmp == 0); break;
                case TOK_NEQ: result = (cmp != 0); break;
                case TOK_LT: result = (cmp < 0); break;
                case TOK_GT: result = (cmp > 0); break;
                default: break;
            }
        } else {
            switch (op.type) {
                case TOK_EQ: result = (left.num == right.num); break;
                case TOK_NEQ: result = (left.num != right.num); break;
                case TOK_LT: result = (left.num < right.num); break;
                case TOK_GT: result = (left.num > right.num); break;
                case TOK_LTE: result = (left.num <= right.num); break;
                case TOK_GTE: result = (left.num >= right.num); break;
                default: break;
            }
        }
        return make_num(result ? 1 : 0);
    }
    return left;
}

static Value parse_expr() {
    Value left = parse_comparison();
    while (!had_error) {
        Token op = peek_token();
        if (op.type == TOK_AND) {
            next_token();
            Value right = parse_comparison();
            left = make_num((left.num && right.num) ? 1 : 0);
        } else if (op.type == TOK_OR) {
            next_token();
            Value right = parse_comparison();
            left = make_num((left.num || right.num) ? 1 : 0);
        } else break;
    }
    return left;
}

// --- Find matching brace ---
static int find_matching_brace(int from_line) {
    int depth = 0;
    for (int i = from_line; i < line_count; i++) {
        for (int j = 0; source_lines[i][j]; j++) {
            if (source_lines[i][j] == '{') depth++;
            if (source_lines[i][j] == '}') {
                depth--;
                if (depth == 0) return i;
            }
        }
    }
    return from_line;
}

// --- Execute script ---
static void execute() {
    had_error = false;
    current_line = 0;
    call_depth = 0;

    // First pass: register functions
    for (int i = 0; i < line_count; i++) {
        line_src = source_lines[i];
        line_pos = 0;
        Token tok = peek_token();
        if (tok.type == TOK_FUNC && func_count < MAX_FUNCS) {
            next_token();  // consume 'func'
            Token name = next_token();
            Function& fn = funcs[func_count];
            neo_strcpy(fn.name, name.str_val);
            fn.param_count = 0;

            Token lp = next_token();  // (
            if (lp.type == TOK_LPAREN) {
                Token pt = peek_token();
                while (pt.type == TOK_IDENT) {
                    Token param = next_token();
                    neo_strcpy(fn.params[fn.param_count++], param.str_val);
                    pt = peek_token();
                    if (pt.type == TOK_COMMA) { next_token(); pt = peek_token(); }
                }
                next_token();  // )
            }

            fn.start_line = i + 1;
            fn.end_line = find_matching_brace(i);
            fn.defined = true;
            func_count++;

            i = fn.end_line;  // Skip function body
        }
    }

    // Second pass: execute
    current_line = 0;
    while (current_line < line_count && !had_error) {
        line_src = source_lines[current_line];
        line_pos = 0;
        skip_whitespace();

        if (line_src[line_pos] == 0 || line_src[line_pos] == '#' ||
            line_src[line_pos] == '{' || line_src[line_pos] == '}') {
            current_line++;
            continue;
        }

        Token tok = peek_token();

        // Skip function definitions
        if (tok.type == TOK_FUNC) {
            current_line = find_matching_brace(current_line) + 1;
            continue;
        }

        // var x = expr
        if (tok.type == TOK_VAR) {
            next_token();
            Token name = next_token();
            Token eq = next_token();
            if (eq.type == TOK_ASSIGN) {
                Value val = parse_expr();
                Variable* v = find_var(name.str_val);
                if (!v) v = create_var(name.str_val);
                if (v) v->val = val;
            }
        }
        // print / println
        else if (tok.type == TOK_PRINT || tok.type == TOK_PRINTLN) {
            next_token();
            Value v = parse_expr();
            if (v.type == VAL_STR) neo::display::printf("%s", v.str);
            else neo::display::printf("%ld", v.num);
            if (tok.type == TOK_PRINTLN) neo::display::putchar('\n');
        }
        // if
        else if (tok.type == TOK_IF) {
            next_token();
            Value cond = parse_expr();
            int brace_end = find_matching_brace(current_line);
            int else_line = -1;

            // Look for else
            if (brace_end + 1 < line_count) {
                const char* nxt = source_lines[brace_end + 1];
                int np = 0;
                while (nxt[np] == ' ' || nxt[np] == '\t') np++;
                if (neo_strncmp(nxt + np, "else", 4) == 0) {
                    else_line = brace_end + 1;
                }
            }

            if (cond.num) {
                current_line++;  // Enter if body
                // Will naturally execute until }
            } else {
                if (else_line >= 0) {
                    current_line = else_line + 1;
                } else {
                    current_line = brace_end + 1;
                }
                continue;
            }
        }
        // while
        else if (tok.type == TOK_WHILE) {
            int loop_start = current_line;
            next_token();
            int cond_pos = line_pos;
            Value cond = parse_expr();
            int brace_end = find_matching_brace(current_line);

            if (!cond.num) {
                current_line = brace_end + 1;
                continue;
            }

            // Execute body
            int saved_start = loop_start;
            int saved_end = brace_end;
            current_line++;

            while (current_line < saved_end && !had_error) {
                line_src = source_lines[current_line];
                line_pos = 0;
                skip_whitespace();
                if (line_src[line_pos] == 0 || line_src[line_pos] == '#') {
                    current_line++;
                    continue;
                }

                Token inner = peek_token();

                if (inner.type == TOK_VAR) {
                    next_token();
                    Token nm = next_token();
                    Token eq = next_token();
                    if (eq.type == TOK_ASSIGN) {
                        Value val = parse_expr();
                        Variable* v = find_var(nm.str_val);
                        if (!v) v = create_var(nm.str_val);
                        if (v) v->val = val;
                    }
                } else if (inner.type == TOK_PRINT || inner.type == TOK_PRINTLN) {
                    next_token();
                    Value v = parse_expr();
                    if (v.type == VAL_STR) neo::display::printf("%s", v.str);
                    else neo::display::printf("%ld", v.num);
                    if (inner.type == TOK_PRINTLN) neo::display::putchar('\n');
                } else if (inner.type == TOK_IDENT) {
                    Token nm = next_token();
                    Token eq = peek_token();
                    if (eq.type == TOK_ASSIGN) {
                        next_token();
                        Value val = parse_expr();
                        Variable* v = find_var(nm.str_val);
                        if (!v) v = create_var(nm.str_val);
                        if (v) v->val = val;
                    } else {
                        // Could be function call - re-parse
                        line_pos = 0;
                        parse_expr();
                    }
                }

                current_line++;
            }

            // Check condition again
            line_src = source_lines[saved_start];
            line_pos = cond_pos;
            cond = parse_expr();
            if (cond.num) {
                current_line = saved_start;
            } else {
                current_line = saved_end + 1;
            }
            continue;
        }
        // Assignment: ident = expr
        else if (tok.type == TOK_IDENT) {
            Token name = next_token();
            Token eq = peek_token();
            if (eq.type == TOK_ASSIGN) {
                next_token();
                Value val = parse_expr();
                Variable* v = find_var(name.str_val);
                if (!v) v = create_var(name.str_val);
                if (v) v->val = val;
            } else {
                // Might be a function call
                line_pos = 0;
                parse_expr();
            }
        }

        current_line++;
    }
}

// --- Load script from file ---
static bool load_file(const char* path) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) return false;

    char buf[8192];
    int bytes = neo::filesystem::read(fh, (unsigned char*)buf, sizeof(buf) - 1);
    neo::filesystem::close(fh);
    if (bytes <= 0) return false;
    buf[bytes] = 0;

    line_count = 0;
    int bi = 0, li = 0;
    while (bi < bytes && line_count < MAX_LINES) {
        if (buf[bi] == '\n' || buf[bi] == 0) {
            source_lines[line_count][li] = 0;
            line_count++;
            li = 0;
            bi++;
        } else {
            if (li < MAX_LINE_LEN - 1) {
                source_lines[line_count][li++] = buf[bi];
            }
            bi++;
        }
    }
    if (li > 0 && line_count < MAX_LINES) {
        source_lines[line_count][li] = 0;
        line_count++;
    }

    return true;
}

// --- REPL ---
static void repl() {
    neo::display::set_fg(11);
    neo::display::printf("NeoScript v1.0 - Interactive REPL\n");
    neo::display::printf("Type 'quit' to exit, 'help' for commands.\n\n");
    neo::display::set_fg(7);

    char line[MAX_LINE_LEN];

    while (true) {
        neo::display::set_fg(10);
        neo::display::printf("ns> ");
        neo::display::set_fg(7);

        neo::console::getline(line, sizeof(line), nullptr);

        if (neo_strcmp(line, "quit") == 0 || neo_strcmp(line, "exit") == 0) break;

        if (neo_strcmp(line, "help") == 0) {
            neo::display::printf("  var x = <expr>     Declare variable\n");
            neo::display::printf("  x = <expr>         Assign variable\n");
            neo::display::printf("  print <expr>       Print value\n");
            neo::display::printf("  println <expr>     Print with newline\n");
            neo::display::printf("  if <expr> { ... }  Conditional\n");
            neo::display::printf("  while <expr> {...} Loop\n");
            neo::display::printf("  func name(a) {...} Define function\n");
            neo::display::printf("  len(str)           String length\n");
            neo::display::printf("  substr(s,i,n)      Substring\n");
            neo::display::printf("  input(prompt)      Read input\n");
            neo::display::printf("  vars               Show variables\n");
            neo::display::printf("  run <file>         Run script file\n");
            neo::display::printf("  quit               Exit REPL\n");
            continue;
        }

        if (neo_strcmp(line, "vars") == 0) {
            for (int i = 0; i < MAX_VARS; i++) {
                if (vars[i].used) {
                    if (vars[i].val.type == VAL_STR)
                        neo::display::printf("  %s = \"%s\"\n", vars[i].name, vars[i].val.str);
                    else
                        neo::display::printf("  %s = %ld\n", vars[i].name, vars[i].val.num);
                }
            }
            continue;
        }

        if (neo_strncmp(line, "run ", 4) == 0) {
            if (load_file(line + 4)) {
                execute();
            } else {
                neo::display::set_fg(12);
                neo::display::printf("Cannot open '%s'\n", line + 4);
                neo::display::set_fg(7);
            }
            continue;
        }

        // Execute single line
        neo_strcpy(source_lines[0], line);
        line_count = 1;
        had_error = false;
        current_line = 0;
        line_src = source_lines[0];
        line_pos = 0;

        Token tok = peek_token();

        if (tok.type == TOK_VAR) {
            next_token();
            Token name = next_token();
            Token eq = next_token();
            if (eq.type == TOK_ASSIGN) {
                Value val = parse_expr();
                Variable* v = find_var(name.str_val);
                if (!v) v = create_var(name.str_val);
                if (v) v->val = val;
            }
        } else if (tok.type == TOK_PRINT || tok.type == TOK_PRINTLN) {
            next_token();
            Value v = parse_expr();
            if (v.type == VAL_STR) neo::display::printf("%s", v.str);
            else neo::display::printf("%ld", v.num);
            if (tok.type == TOK_PRINTLN) neo::display::putchar('\n');
        } else if (tok.type == TOK_IDENT) {
            Token name = next_token();
            Token eq = peek_token();
            if (eq.type == TOK_ASSIGN) {
                next_token();
                Value val = parse_expr();
                Variable* v = find_var(name.str_val);
                if (!v) v = create_var(name.str_val);
                if (v) v->val = val;
            } else {
                line_pos = 0;
                Value result = parse_expr();
                if (!had_error) {
                    if (result.type == VAL_STR)
                        neo::display::printf("=> \"%s\"\n", result.str);
                    else
                        neo::display::printf("=> %ld\n", result.num);
                }
            }
        } else if (tok.type != TOK_EOF) {
            Value result = parse_expr();
            if (!had_error) {
                if (result.type == VAL_STR)
                    neo::display::printf("=> \"%s\"\n", result.str);
                else
                    neo::display::printf("=> %ld\n", result.num);
            }
        }
    }
}

// --- Init ---
static void init() {
    for (int i = 0; i < MAX_VARS; i++) vars[i].used = false;
    func_count = 0;
    call_depth = 0;
    had_error = false;
    line_count = 0;
}

}  // namespace neoscript

extern "C" void app_main(int argc, char** argv) {
    neoscript::init();

    if (argc > 1) {
        // Run script file
        if (neoscript::load_file(argv[1])) {
            neoscript::execute();
            if (neoscript::had_error) {
                neo::display::set_fg(12);
                neo::display::printf("\nScript failed at line %d: %s\n",
                    neoscript::error_line, neoscript::error_msg);
                neo::display::set_fg(7);
            }
        } else {
            neo::display::set_fg(12);
            neo::display::printf("Error: Cannot open '%s'\n", argv[1]);
            neo::display::set_fg(7);
        }
        return;
    }

    // Interactive REPL
    neoscript::repl();
}
