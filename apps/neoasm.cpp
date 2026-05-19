#include "../include/neobench.h"
#include "../lib/string.h"

// NeoASM - On-device 68K Assembler
// Two-pass assembler with label support, directives, and binary output

namespace neoasm {

static const int MAX_SYMBOLS = 512;
static const int MAX_LINES = 2048;
static const int MAX_OUTPUT = 65536;
static const int MAX_LINE_LEN = INODE_SIZE;
static const int MAX_ERRORS = 64;
static const int MAX_FIXUPS = INODE_SIZE;

enum TokenType {
    TOK_NONE, TOK_LABEL, TOK_INSTR, TOK_REG_D, TOK_REG_A,
    TOK_IMM, TOK_NUMBER, TOK_STRING, TOK_COMMA, TOK_LPAREN,
    TOK_RPAREN, TOK_PLUS, TOK_MINUS, TOK_HASH, TOK_DOT,
    TOK_DIRECTIVE, TOK_IDENT, TOK_EOF
};

enum AddrMode {
    AM_DN = 0,       // Dn
    AM_AN,           // An
    AM_AN_IND,       // (An)
    AM_AN_POST,      // (An)+
    AM_AN_PRE,       // -(An)
    AM_AN_DISP,      // d(An)
    AM_IMM,          // #imm
    AM_ABS,          // absolute
    AM_NONE
};

struct Symbol {
    char name[64];
    unsigned int value;
    bool defined;
    bool is_equ;
};

struct Operand {
    AddrMode mode;
    int reg;
    int value;
    bool has_value;
};

struct Error {
    int line;
    char msg[128];
};

struct Fixup {
    int offset;
    int line;
    char label[64];
    int size; // 1=byte, 2=word, 4=long
    bool is_relative;
    int instr_offset;
};

struct AsmState {
    Symbol symbols[MAX_SYMBOLS];
    int num_symbols;
    Error errors[MAX_ERRORS];
    int num_errors;
    Fixup fixups[MAX_FIXUPS];
    int num_fixups;
    unsigned char* output;
    int output_pos;
    int origin;
    int current_line;
    int pass;
    bool verbose;
};

static AsmState* state = nullptr;

// --- Utility functions ---

static bool is_space(char c) { return c == ' ' || c == '\t'; }
static bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }
static bool is_hex(char c) { return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static void str_upper(char* dst, const char* src) {
    while (*src) { *dst++ = to_upper(*src++); }
    *dst = 0;
}

static int parse_int(const char* s, int* val) {
    int v = 0; int sign = 1; const char* p = s;
    if (*p == '-') { sign = -1; p++; }
    if (*p == '$' || (*p == '0' && to_upper(*(p+1)) == 'X')) {
        p += (*p == '$') ? 1 : 2;
        while (is_hex(*p)) {
            int d = is_digit(*p) ? (*p - '0') : (to_upper(*p) - 'A' + 10);
            v = (v << 4) | d; p++;
        }
    } else if (*p == '%') {
        p++;
        while (*p == '0' || *p == '1') { v = (v << 1) | (*p - '0'); p++; }
    } else {
        while (is_digit(*p)) { v = v * 10 + (*p - '0'); p++; }
    }
    *val = v * sign;
    return (int)(p - s);
}

static void add_error(const char* msg) {
    if (state->num_errors >= MAX_ERRORS) return;
    Error& e = state->errors[state->num_errors++];
    e.line = state->current_line;
    neo_strncpy(e.msg, msg, 127);
    e.msg[127] = 0;
}

static Symbol* find_symbol(const char* name) {
    char upper[64];
    str_upper(upper, name);
    for (int i = 0; i < state->num_symbols; i++) {
        char su[64];
        str_upper(su, state->symbols[i].name);
        if (neo_strcmp(upper, su) == 0) return &state->symbols[i];
    }
    return nullptr;
}

static Symbol* add_symbol(const char* name, unsigned int value, bool is_equ) {
    Symbol* existing = find_symbol(name);
    if (existing) {
        if (existing->defined && state->pass == 0) {
            add_error("Duplicate symbol");
            return existing;
        }
        existing->value = value;
        existing->defined = true;
        return existing;
    }
    if (state->num_symbols >= MAX_SYMBOLS) {
        add_error("Too many symbols");
        return nullptr;
    }
    Symbol& s = state->symbols[state->num_symbols++];
    neo_strncpy(s.name, name, 63);
    s.name[63] = 0;
    s.value = value;
    s.defined = true;
    s.is_equ = is_equ;
    return &s;
}

static void emit_byte(unsigned char b) {
    if (state->output_pos < MAX_OUTPUT) {
        state->output[state->output_pos++] = b;
    }
}

static void emit_word(unsigned short w) {
    emit_byte((w >> 8) & 0xFF);
    emit_byte(w & 0xFF);
}

static void emit_long(unsigned int l) {
    emit_word((l >> 16) & 0xFFFF);
    emit_word(l & 0xFFFF);
}

static void add_fixup(const char* label, int offset, int size, bool relative, int instr_off) {
    if (state->num_fixups >= MAX_FIXUPS) { add_error("Too many fixups"); return; }
    Fixup& f = state->fixups[state->num_fixups++];
    neo_strncpy(f.label, label, 63);
    f.label[63] = 0;
    f.offset = offset;
    f.size = size;
    f.is_relative = relative;
    f.instr_offset = instr_off;
    f.line = state->current_line;
}

// --- Tokenizer ---

static const char* skip_ws(const char* p) {
    while (is_space(*p)) p++;
    return p;
}

static const char* read_ident(const char* p, char* buf, int max) {
    int i = 0;
    while (is_alnum(*p) || *p == '_' || *p == '.') {
        if (i < max - 1) buf[i++] = *p;
        p++;
    }
    buf[i] = 0;
    return p;
}

// --- Instruction encoding ---

struct InstrDef {
    const char* mnemonic;
    int opcode_base;
    int type; // 0=two_op, 1=one_op, 2=branch, 3=no_op, 4=special
};

static const InstrDef instructions[] = {
    {"MOVE",  0x0000, 0}, {"ADD",   0xD000, 0}, {"SUB",   0x9000, 0},
    {"MULU",  0xC0C0, 0}, {"MULS",  0xC1C0, 0}, {"DIVU",  0x80C0, 0},
    {"DIVS",  0x81C0, 0}, {"AND",   0xC000, 0}, {"OR",    0x8000, 0},
    {"EOR",   0xB100, 0}, {"NOT",   0x4600, 1}, {"LSL",   0xE108, 0},
    {"LSR",   0xE008, 0}, {"ASL",   0xE100, 0}, {"ASR",   0xE000, 0},
    {"BRA",   0x6000, 2}, {"BEQ",   0x6700, 2}, {"BNE",   0x6600, 2},
    {"BGT",   0x6E00, 2}, {"BLT",   0x6D00, 2}, {"BGE",   0x6C00, 2},
    {"BLE",   0x6F00, 2}, {"BSR",   0x6100, 2}, {"RTS",   0x4E75, 3},
    {"JMP",   0x4EC0, 1}, {"JSR",   0x4E80, 1}, {"NOP",   0x4E71, 3},
    {"LEA",   0x41C0, 4}, {"PEA",   0x4840, 1}, {"CMP",   0xB000, 0},
    {"TST",   0x4A00, 1}, {"CLR",   0x4200, 1}, {"SWAP",  0x4840, 4},
    {"EXT",   0x4800, 4}, {"MOVEM", 0x48A0, 4}, {"LINK",  0x4E50, 4},
    {"UNLK",  0x4E58, 4}, {nullptr, 0, 0}
};

static int get_size_bits(char sz) {
    switch (sz) {
        case 'B': return 0;
        case 'W': return 1;
        case 'L': return 2;
        default: return 1; // default word
    }
}

static int encode_ea(Operand* op) {
    switch (op->mode) {
        case AM_DN:      return op->reg;
        case AM_AN:      return 0x08 | op->reg;
        case AM_AN_IND:  return 0x10 | op->reg;
        case AM_AN_POST: return 0x18 | op->reg;
        case AM_AN_PRE:  return 0x20 | op->reg;
        case AM_AN_DISP: return 0x28 | op->reg;
        case AM_IMM:     return 0x3C;
        case AM_ABS:     return 0x39;
        default:         return 0;
    }
}

static bool parse_operand(const char** pp, Operand* op) {
    const char* p = skip_ws(*pp);
    op->mode = AM_NONE;
    op->reg = 0;
    op->value = 0;
    op->has_value = false;

    if (*p == '#') {
        // Immediate
        p++;
        op->mode = AM_IMM;
        if (is_digit(*p) || *p == '$' || *p == '%' || *p == '-') {
            p += parse_int(p, &op->value);
            op->has_value = true;
        } else if (is_alpha(*p)) {
            char ident[64];
            p = read_ident(p, ident, 64);
            Symbol* s = find_symbol(ident);
            if (s && s->defined) {
                op->value = s->value;
                op->has_value = true;
            } else {
                op->value = 0;
                op->has_value = true;
            }
        }
    } else if (*p == '-' && *(p+1) == '(') {
        // -(An)
        p += 2;
        if (to_upper(*p) == 'A' && is_digit(*(p+1))) {
            op->mode = AM_AN_PRE;
            op->reg = *(p+1) - '0';
            p += 2;
            if (*p == ')') p++;
        }
    } else if (*p == '(') {
        // (An) or (An)+
        p++;
        if (to_upper(*p) == 'A' && is_digit(*(p+1))) {
            op->reg = *(p+1) - '0';
            p += 2;
            if (*p == ')') {
                p++;
                if (*p == '+') {
                    op->mode = AM_AN_POST;
                    p++;
                } else {
                    op->mode = AM_AN_IND;
                }
            }
        }
    } else if ((to_upper(*p) == 'D' || to_upper(*p) == 'A') && is_digit(*(p+1)) && !is_alnum(*(p+2))) {
        // Dn or An
        if (to_upper(*p) == 'D') {
            op->mode = AM_DN;
        } else {
            op->mode = AM_AN;
        }
        op->reg = *(p+1) - '0';
        p += 2;
    } else if (is_digit(*p) || *p == '$' || *p == '%') {
        // Number - could be absolute or displacement
        int v;
        p += parse_int(p, &v);
        if (*p == '(') {
            // d(An)
            p++;
            if (to_upper(*p) == 'A' && is_digit(*(p+1))) {
                op->mode = AM_AN_DISP;
                op->reg = *(p+1) - '0';
                op->value = v;
                op->has_value = true;
                p += 2;
                if (*p == ')') p++;
            }
        } else {
            op->mode = AM_ABS;
            op->value = v;
            op->has_value = true;
        }
    } else if (is_alpha(*p)) {
        char ident[64];
        p = read_ident(p, ident, 64);
        // Check if it's SP (=A7)
        char upper[64]; str_upper(upper, ident);
        if (neo_strcmp(upper, "SP") == 0) {
            op->mode = AM_AN; op->reg = 7;
        } else {
            // Label/symbol reference as absolute address
            Symbol* s = find_symbol(ident);
            if (s && s->defined) {
                op->mode = AM_ABS;
                op->value = s->value;
                op->has_value = true;
            } else {
                op->mode = AM_ABS;
                op->value = 0;
                op->has_value = true;
            }
        }
    }

    *pp = p;
    return op->mode != AM_NONE;
}

static void assemble_two_op(const InstrDef* def, char size, Operand* src, Operand* dst) {
    int sz = get_size_bits(size);
    char uname[16]; str_upper(uname, def->mnemonic);

    if (neo_strcmp(uname, "MOVE") == 0) {
        // MOVE encoding: size in bits 13-12, dst in 11-6, src in 5-0
        int sz_field = (sz == 0) ? 1 : (sz == 1) ? 3 : 2;
        int src_ea = encode_ea(src);
        int dst_ea = encode_ea(dst);
        int dst_swapped = ((dst_ea & 0x07) << 3) | ((dst_ea >> 3) & 0x07);
        unsigned short opcode = (sz_field << 12) | (dst_swapped << 6) | src_ea;
        emit_word(opcode);
    } else if (neo_strcmp(uname, "ADD") == 0 || neo_strcmp(uname, "SUB") == 0) {
        int base = (neo_strcmp(uname, "ADD") == 0) ? 0xD000 : 0x9000;
        int dreg = dst->reg;
        int direction = 0; // EA to register
        if (dst->mode == AM_DN) {
            if (src->mode == AM_DN || src->mode == AM_IMM || src->mode == AM_AN_IND) {
                direction = 0;
            }
        }
        unsigned short opcode = base | (dreg << 9) | (direction << 8) | (sz << 6) | encode_ea(src);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "CMP") == 0) {
        int dreg = dst->reg;
        unsigned short opcode = 0xB000 | (dreg << 9) | (sz << 6) | encode_ea(src);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "AND") == 0 || neo_strcmp(uname, "OR") == 0) {
        int base = (neo_strcmp(uname, "AND") == 0) ? 0xC000 : 0x8000;
        int dreg = dst->reg;
        unsigned short opcode = base | (dreg << 9) | (sz << 6) | encode_ea(src);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "EOR") == 0) {
        int dreg = src->reg;
        unsigned short opcode = 0xB100 | (dreg << 9) | (sz << 6) | encode_ea(dst);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "MULU") == 0 || neo_strcmp(uname, "MULS") == 0) {
        int dreg = dst->reg;
        int base = (neo_strcmp(uname, "MULU") == 0) ? 0xC0C0 : 0xC1C0;
        unsigned short opcode = base | (dreg << 9) | encode_ea(src);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "DIVU") == 0 || neo_strcmp(uname, "DIVS") == 0) {
        int dreg = dst->reg;
        int base = (neo_strcmp(uname, "DIVU") == 0) ? 0x80C0 : 0x81C0;
        unsigned short opcode = base | (dreg << 9) | encode_ea(src);
        emit_word(opcode);
    } else if (neo_strcmp(uname, "LSL") == 0 || neo_strcmp(uname, "LSR") == 0 ||
               neo_strcmp(uname, "ASL") == 0 || neo_strcmp(uname, "ASR") == 0) {
        int dir = (uname[neo_strlen(uname)-1] == 'L') ? 1 : 0;
        int arith = (uname[0] == 'A') ? 1 : 0;
        if (src->mode == AM_IMM) {
            int count = src->value & 7;
            if (count == 0) count = 8;
            unsigned short opcode = 0xE000 | (count << 9) | (dir << 8) | (sz << 6) | (arith << 4) | dst->reg;
            emit_word(opcode);
        } else {
            unsigned short opcode = 0xE020 | (src->reg << 9) | (dir << 8) | (sz << 6) | (arith << 4) | dst->reg;
            emit_word(opcode);
        }
    } else {
        // Generic two-operand
        emit_word(def->opcode_base | (sz << 6) | encode_ea(src));
    }

    // Emit extension words for source
    if (src->mode == AM_IMM) {
        if (sz == 2) emit_long(src->value);
        else emit_word(src->value & 0xFFFF);
    } else if (src->mode == AM_AN_DISP) {
        emit_word(src->value & 0xFFFF);
    } else if (src->mode == AM_ABS) {
        emit_long(src->value);
    }

    // Emit extension words for dest (if memory)
    if (dst->mode == AM_AN_DISP) {
        emit_word(dst->value & 0xFFFF);
    } else if (dst->mode == AM_ABS) {
        emit_long(dst->value);
    }
}

static void assemble_one_op(const InstrDef* def, char size, Operand* op) {
    int sz = get_size_bits(size);
    char uname[16]; str_upper(uname, def->mnemonic);

    if (neo_strcmp(uname, "NOT") == 0) {
        emit_word(0x4600 | (sz << 6) | encode_ea(op));
    } else if (neo_strcmp(uname, "CLR") == 0) {
        emit_word(0x4200 | (sz << 6) | encode_ea(op));
    } else if (neo_strcmp(uname, "TST") == 0) {
        emit_word(0x4A00 | (sz << 6) | encode_ea(op));
    } else if (neo_strcmp(uname, "JMP") == 0) {
        emit_word(0x4EC0 | encode_ea(op));
    } else if (neo_strcmp(uname, "JSR") == 0) {
        emit_word(0x4E80 | encode_ea(op));
    } else if (neo_strcmp(uname, "PEA") == 0) {
        emit_word(0x4840 | encode_ea(op));
    } else {
        emit_word(def->opcode_base | encode_ea(op));
    }

    if (op->mode == AM_ABS) emit_long(op->value);
    else if (op->mode == AM_AN_DISP) emit_word(op->value & 0xFFFF);
    else if (op->mode == AM_IMM) {
        if (sz == 2) emit_long(op->value);
        else emit_word(op->value & 0xFFFF);
    }
}

static void assemble_branch(const InstrDef* def, Operand* op) {
    int target = op->value;
    int pc = state->origin + state->output_pos;
    int disp = target - (pc + 2);

    if (disp >= -128 && disp <= 127 && disp != 0) {
        emit_word(def->opcode_base | (disp & 0xFF));
    } else {
        emit_word(def->opcode_base);
        emit_word(disp & 0xFFFF);
    }
}

static void assemble_line(const char* line) {
    const char* p = skip_ws(line);
    if (*p == 0 || *p == ';' || *p == '*') return;

    char label[64] = {};
    char instr[32] = {};
    char size = 'W';

    // Check for label
    if (is_alpha(*p)) {
        const char* start = p;
        char ident[64];
        p = read_ident(p, ident, 64);
        p = skip_ws(p);
        if (*p == ':') {
            neo_strcpy(label, ident);
            p++;
            p = skip_ws(p);
        } else if (*p == 0 || *p == ';') {
            // Label on its own line
            add_symbol(ident, state->origin + state->output_pos, false);
            return;
        } else {
            // Check if it's an EQU
            char next_token[64];
            const char* q = p;
            q = read_ident(q, next_token, 64);
            char utoken[64]; str_upper(utoken, next_token);
            if (neo_strcmp(utoken, "EQU") == 0) {
                q = skip_ws(q);
                int val = 0;
                parse_int(q, &val);
                add_symbol(ident, val, true);
                return;
            }
            // Otherwise ident is instruction
            neo_strcpy(instr, ident);
            p = skip_ws(p);
            goto have_instr;
        }
    }

    if (label[0]) {
        add_symbol(label, state->origin + state->output_pos, false);
    }

    if (*p == 0 || *p == ';') return;

    {
        p = read_ident(p, instr, 32);
    }

have_instr:
    // Check for size suffix
    {
        char uinstr[32]; str_upper(uinstr, instr);
        int len = neo_strlen(uinstr);
        if (len > 2 && uinstr[len-2] == '.') {
            size = uinstr[len-1];
            uinstr[len-2] = 0;
            instr[len-2] = 0;
        }
    }

    p = skip_ws(p);

    char uinstr[32]; str_upper(uinstr, instr);

    // Handle directives
    if (neo_strcmp(uinstr, "ORG") == 0) {
        int val; parse_int(p, &val);
        state->origin = val;
        return;
    }
    if (neo_strcmp(uinstr, "SECTION") == 0) {
        return; // Ignore section for now
    }
    if (neo_strcmp(uinstr, "DC") == 0 || neo_strncmp(uinstr, "DC.", 3) == 0) {
        // Parse size from after DC
        char dc_size = size;
        if (neo_strlen(instr) > 2 && instr[2] == '.') {
            dc_size = to_upper(instr[3]);
        }
        // Parse values
        while (*p && *p != ';') {
            p = skip_ws(p);
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                while (*p && *p != quote) {
                    emit_byte(*p++);
                }
                if (*p == quote) p++;
            } else {
                int val; p += parse_int(p, &val);
                if (dc_size == 'B') emit_byte(val & 0xFF);
                else if (dc_size == 'W') emit_word(val & 0xFFFF);
                else emit_long(val);
            }
            p = skip_ws(p);
            if (*p == ',') p++;
        }
        return;
    }
    if (neo_strcmp(uinstr, "DS") == 0 || neo_strncmp(uinstr, "DS.", 3) == 0) {
        char ds_size = size;
        if (neo_strlen(instr) > 2 && instr[2] == '.') {
            ds_size = to_upper(instr[3]);
        }
        int count; parse_int(p, &count);
        int bytes = count;
        if (ds_size == 'W') bytes *= 2;
        else if (ds_size == 'L') bytes *= 4;
        for (int i = 0; i < bytes; i++) emit_byte(0);
        return;
    }

    // Find instruction
    const InstrDef* def = nullptr;
    for (int i = 0; instructions[i].mnemonic; i++) {
        if (neo_strcmp(uinstr, instructions[i].mnemonic) == 0) {
            def = &instructions[i];
            break;
        }
    }

    if (!def) {
        add_error("Unknown instruction");
        return;
    }

    // Parse operands
    Operand src, dst;
    src.mode = AM_NONE; dst.mode = AM_NONE;

    if (def->type == 3) {
        // No operands (RTS, NOP)
        emit_word(def->opcode_base);
        return;
    }

    parse_operand(&p, &src);
    p = skip_ws(p);
    if (*p == ',') {
        p++;
        parse_operand(&p, &dst);
    }

    if (def->type == 4) {
        // Special instructions
        if (neo_strcmp(uinstr, "LEA") == 0) {
            int areg = dst.reg;
            unsigned short opcode = 0x41C0 | (areg << 9) | encode_ea(&src);
            emit_word(opcode);
            if (src.mode == AM_ABS) emit_long(src.value);
            else if (src.mode == AM_AN_DISP) emit_word(src.value & 0xFFFF);
        } else if (neo_strcmp(uinstr, "SWAP") == 0) {
            emit_word(0x4840 | src.reg);
        } else if (neo_strcmp(uinstr, "EXT") == 0) {
            int opmode = (size == 'L') ? 3 : 2;
            emit_word(0x4800 | (opmode << 6) | src.reg);
        } else if (neo_strcmp(uinstr, "LINK") == 0) {
            emit_word(0x4E50 | src.reg);
            emit_word(dst.value & 0xFFFF);
        } else if (neo_strcmp(uinstr, "UNLK") == 0) {
            emit_word(0x4E58 | src.reg);
        } else if (neo_strcmp(uinstr, "MOVEM") == 0) {
            // Simplified: emit placeholder
            emit_word(0x48A0);
            emit_word(0xFFFF); // register mask placeholder
        }
        return;
    }

    if (def->type == 2) {
        // Branch
        assemble_branch(def, &src);
    } else if (def->type == 1) {
        assemble_one_op(def, size, &src);
    } else {
        assemble_two_op(def, size, &src, &dst);
    }
}

static void show_help() {
    neo::display::printf("NeoASM - 68K Assembler v1.0\n");
    neo::display::printf("Usage: neoasm <command> [args]\n\n");
    neo::display::printf("Commands:\n");
    neo::display::printf("  asm <file>      - Assemble source file to binary\n");
    neo::display::printf("  symbols <file>  - Show symbol table for source\n");
    neo::display::printf("  check <file>    - Syntax check only (no output)\n");
    neo::display::printf("  interactive     - Interactive assembler mode\n");
    neo::display::printf("  help            - Show instruction reference\n\n");
    neo::display::printf("Supported instructions:\n");
    neo::display::printf("  Data: MOVE, CLR, SWAP, EXT, LEA, PEA\n");
    neo::display::printf("  Arith: ADD, SUB, MULU, MULS, DIVU, DIVS, CMP, TST\n");
    neo::display::printf("  Logic: AND, OR, EOR, NOT\n");
    neo::display::printf("  Shift: LSL, LSR, ASL, ASR\n");
    neo::display::printf("  Branch: BRA, BEQ, BNE, BGT, BLT, BGE, BLE\n");
    neo::display::printf("  Control: JMP, JSR, BSR, RTS, NOP, LINK, UNLK\n");
    neo::display::printf("  Other: MOVEM\n\n");
    neo::display::printf("Directives: ORG, EQU, DC.B/W/L, DS.B/W/L, SECTION\n");
    neo::display::printf("Addressing: Dn, An, (An), (An)+, -(An), d(An), #imm\n");
}

static void interactive_mode() {
    neo::display::clear();
    neo::display::printf("+----------------------------------------------+\n");
    neo::display::printf("|          NeoASM Interactive Assembler        |\n");
    neo::display::printf("+----------------------------------------------+\n\n");
    neo::display::printf("Enter 68K assembly instructions one per line.\n");
    neo::display::printf("Commands: .list .symbols .reset .hex .save <file> .quit\n\n");

    state = (AsmState*)neo::mem::alloc(sizeof(AsmState));
    neo_memset(state, 0, sizeof(AsmState));
    state->output = (unsigned char*)neo::mem::alloc(MAX_OUTPUT);
    state->origin = 0;

    char lines[INODE_SIZE][MAX_LINE_LEN];
    int line_count = 0;
    char input[MAX_LINE_LEN];

    while (true) {
        char prompt[32];
        ksprintf(prompt, 32, "%04X> ", state->origin + state->output_pos);
        neo::console::getline(input, MAX_LINE_LEN, prompt);

        if (input[0] == 0) continue;
        neo::console::history_add(input);

        if (input[0] == '.') {
            char cmd[32]; const char* p = input + 1;
            p = read_ident(p, cmd, 32);
            char ucmd[32]; str_upper(ucmd, cmd);

            if (neo_strcmp(ucmd, "QUIT") == 0 || neo_strcmp(ucmd, "Q") == 0) break;
            if (neo_strcmp(ucmd, "LIST") == 0) {
                neo::display::printf("\n--- Listing ---\n");
                for (int i = 0; i < line_count; i++) {
                    neo::display::printf("%4d: %s\n", i + 1, lines[i]);
                }
                neo::display::printf("--- %d lines, %d bytes ---\n\n", line_count, state->output_pos);
                continue;
            }
            if (neo_strcmp(ucmd, "SYMBOLS") == 0) {
                neo::display::printf("\n--- Symbol Table ---\n");
                for (int i = 0; i < state->num_symbols; i++) {
                    Symbol& s = state->symbols[i];
                    neo::display::printf("  %-20s = $%08X", s.name, s.value);
                    if (s.is_equ) neo::display::printf(" (EQU)");
                    neo::display::printf("\n");
                }
                neo::display::printf("--- %d symbols ---\n\n", state->num_symbols);
                continue;
            }
            if (neo_strcmp(ucmd, "RESET") == 0) {
                if (state->output) neo::mem::free(state->output);
                neo_memset(state, 0, sizeof(AsmState));
                state->output = (unsigned char*)neo::mem::alloc(MAX_OUTPUT);
                line_count = 0;
                neo::display::printf("Assembler state reset.\n");
                continue;
            }
            if (neo_strcmp(ucmd, "HEX") == 0) {
                neo::display::printf("\n--- Hex Dump (%d bytes) ---\n", state->output_pos);
                for (int i = 0; i < state->output_pos; i++) {
                    if (i % 16 == 0) neo::display::printf("%04X: ", state->origin + i);
                    neo::display::printf("%02X ", state->output[i]);
                    if (i % 16 == 15 || i == state->output_pos - 1) neo::display::printf("\n");
                }
                neo::display::printf("---\n\n");
                continue;
            }
            if (neo_strcmp(ucmd, "SAVE") == 0) {
                p = skip_ws(p);
                char filename[128];
                int fi = 0;
                while (*p && !is_space(*p) && fi < 127) filename[fi++] = *p++;
                filename[fi] = 0;
                if (fi == 0) {
                    neo::display::printf("Usage: .save <filename>\n");
                    continue;
                }
                neo::filesystem::FileHandle fh;
                if (neo::filesystem::open(fh, filename, 1) == 0) {
                    neo::filesystem::write(fh, state->output, state->output_pos);
                    neo::filesystem::close(fh);
                    neo::display::printf("Saved %d bytes to %s\n", state->output_pos, filename);
                } else {
                    neo::display::printf("Error: Cannot open %s for writing\n", filename);
                }
                continue;
            }
            neo::display::printf("Unknown command: .%s\n", cmd);
            continue;
        }

        // Assemble the line
        if (line_count < INODE_SIZE) {
            neo_strncpy(lines[line_count], input, MAX_LINE_LEN - 1);
            lines[line_count][MAX_LINE_LEN - 1] = 0;
            line_count++;
        }

        int prev_pos = state->output_pos;
        int prev_errors = state->num_errors;
        state->current_line = line_count;
        state->pass = 1;

        assemble_line(input);

        if (state->num_errors > prev_errors) {
            for (int i = prev_errors; i < state->num_errors; i++) {
                neo::display::set_fg(1); // Red
                neo::display::printf("Error: %s\n", state->errors[i].msg);
                neo::display::set_fg(7); // White
            }
        } else {
            // Show assembled bytes
            neo::display::set_fg(2); // Green
            neo::display::printf("  ");
            for (int i = prev_pos; i < state->output_pos; i++) {
                neo::display::printf("%02X ", state->output[i]);
            }
            neo::display::printf("\n");
            neo::display::set_fg(7);
        }
    }

    neo::mem::free(state->output);
    neo::mem::free(state);
}

static void assemble_file(const char* filename) {
    neo::display::printf("NeoASM: Assembling %s\n", filename);

    // Read source file
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, filename, 0) != 0) {
        neo::display::printf("Error: Cannot open %s\n", filename);
        return;
    }

    char* source = (char*)neo::mem::alloc(64 * 1024);
    int bytes_read = neo::filesystem::read(fh, source, 64 * 1024 - 1);
    neo::filesystem::close(fh);
    source[bytes_read] = 0;

    state = (AsmState*)neo::mem::alloc(sizeof(AsmState));
    neo_memset(state, 0, sizeof(AsmState));
    state->output = (unsigned char*)neo::mem::alloc(MAX_OUTPUT);

    // Split into lines and assemble two passes
    for (int pass = 0; pass < 2; pass++) {
        state->output_pos = 0;
        state->num_fixups = 0;
        if (pass == 1) state->num_errors = 0;
        state->pass = pass;
        state->current_line = 0;

        const char* p = source;
        while (*p) {
            state->current_line++;
            char line[MAX_LINE_LEN];
            int li = 0;
            while (*p && *p != '\n' && li < MAX_LINE_LEN - 1) {
                line[li++] = *p++;
            }
            line[li] = 0;
            if (*p == '\n') p++;

            assemble_line(line);
        }
    }

    // Report results
    if (state->num_errors > 0) {
        neo::display::set_fg(1);
        neo::display::printf("\nAssembly FAILED with %d error(s):\n", state->num_errors);
        for (int i = 0; i < state->num_errors; i++) {
            neo::display::printf("  Line %d: %s\n", state->errors[i].line, state->errors[i].msg);
        }
        neo::display::set_fg(7);
    } else {
        // Write output
        char outname[128];
        neo_strncpy(outname, filename, 120);
        // Replace extension with .bin
        int len = neo_strlen(outname);
        for (int i = len - 1; i >= 0; i--) {
            if (outname[i] == '.') { outname[i] = 0; break; }
        }
        neo_strcat(outname, ".bin");

        neo::filesystem::FileHandle ofh;
        if (neo::filesystem::open(ofh, outname, 1) == 0) {
            neo::filesystem::write(ofh, state->output, state->output_pos);
            neo::filesystem::close(ofh);
        }

        neo::display::set_fg(2);
        neo::display::printf("\nAssembly successful!\n");
        neo::display::printf("  Output: %s\n", outname);
        neo::display::printf("  Size: %d bytes\n", state->output_pos);
        neo::display::printf("  Symbols: %d\n", state->num_symbols);
        neo::display::set_fg(7);

        // Symbol table dump
        if (state->num_symbols > 0) {
            neo::display::printf("\nSymbol Table:\n");
            neo::display::printf("  %-24s  %-10s  %s\n", "Name", "Value", "Type");
            neo::display::printf("  %-24s  %-10s  %s\n", "------------------------", "----------", "----");
            for (int i = 0; i < state->num_symbols; i++) {
                Symbol& s = state->symbols[i];
                neo::display::printf("  %-24s  $%08X  %s\n", s.name, s.value, s.is_equ ? "EQU" : "Label");
            }
        }
    }

    neo::mem::free(source);
    neo::mem::free(state->output);
    neo::mem::free(state);
}

} // namespace neoasm

extern "C" void app_main(int argc, char** argv) {
    if (argc < 2) {
        neoasm::show_help();
        return;
    }

    char cmd[32];
    neoasm::str_upper(cmd, argv[1]);

    if (neo_strcmp(cmd, "HELP") == 0) {
        neoasm::show_help();
    } else if (neo_strcmp(cmd, "INTERACTIVE") == 0 || neo_strcmp(cmd, "I") == 0) {
        neoasm::interactive_mode();
    } else if (neo_strcmp(cmd, "ASM") == 0) {
        if (argc < 3) {
            neo::display::printf("Usage: neoasm asm <file.s>\n");
            return;
        }
        neoasm::assemble_file(argv[2]);
    } else if (neo_strcmp(cmd, "SYMBOLS") == 0) {
        if (argc < 3) {
            neo::display::printf("Usage: neoasm symbols <file.s>\n");
            return;
        }
        // Run pass 1 only and show symbols
        neoasm::state = (neoasm::AsmState*)neo::mem::alloc(sizeof(neoasm::AsmState));
        neo_memset(neoasm::state, 0, sizeof(neoasm::AsmState));
        neoasm::state->output = (unsigned char*)neo::mem::alloc(neoasm::MAX_OUTPUT);
        neoasm::assemble_file(argv[2]);
        neo::mem::free(neoasm::state->output);
        neo::mem::free(neoasm::state);
    } else if (neo_strcmp(cmd, "CHECK") == 0) {
        if (argc < 3) {
            neo::display::printf("Usage: neoasm check <file.s>\n");
            return;
        }
        neoasm::assemble_file(argv[2]);
    } else {
        // Assume it's a filename
        neoasm::assemble_file(argv[1]);
    }
}
