#include "../include/neobench.h"
#include "../lib/string.h"

// NeoDebug - Visual Debugger / Memory Inspector for 68K

namespace neodebug {

static const int MAX_BREAKPOINTS = 32;
static const int MAX_WATCHES = 16;
static const int MAX_CALLSTACK = 32;
static const int MAX_SEARCH_LEN = 64;

struct Registers {
    unsigned int d[8];
    unsigned int a[8];
    unsigned int pc;
    unsigned short sr;
};

struct Breakpoint {
    unsigned int address;
    bool enabled;
    bool active;
    int hit_count;
    char label[32];
};

struct Watch {
    unsigned int address;
    int size; // 1, 2, 4
    unsigned int last_value;
    bool active;
    char name[32];
};

struct CallEntry {
    unsigned int from_addr;
    unsigned int to_addr;
};

struct DebugState {
    Registers regs;
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int num_breakpoints;
    Watch watches[MAX_WATCHES];
    int num_watches;
    CallEntry callstack[MAX_CALLSTACK];
    int callstack_depth;
    unsigned int mem_view_addr;
    int mem_view_cols;
    int mem_view_rows;
    unsigned int disasm_addr;
    bool running;
    int panel; // 0=memory, 1=disasm, 2=registers, 3=breakpoints
};

static DebugState* dbg = nullptr;

// --- Utility ---

static bool is_space(char c) { return c == ' ' || c == '\t'; }
static bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static char to_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static unsigned int parse_hex(const char* s) {
    unsigned int v = 0;
    if (*s == '$') s++;
    else if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) s += 2;
    while (is_hex(*s)) {
        int d = is_digit(*s) ? (*s - '0') : (to_upper(*s) - 'A' + 10);
        v = (v << 4) | d;
        s++;
    }
    return v;
}

static unsigned int parse_number(const char* s) {
    if (*s == '$' || (*s == '0' && to_upper(*(s+1)) == 'X')) return parse_hex(s);
    unsigned int v = 0;
    while (is_digit(*s)) { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static const char* skip_ws(const char* p) {
    while (is_space(*p)) p++;
    return p;
}

// --- 68K Disassembler ---

struct DisasmResult {
    char text[80];
    int length; // instruction length in bytes
};

static unsigned char mem_read_byte(unsigned int addr) {
    return *((volatile unsigned char*)addr);
}

static unsigned short mem_read_word(unsigned int addr) {
    return *((volatile unsigned short*)(addr & ~1));
}

static unsigned int mem_read_long(unsigned int addr) {
    return *((volatile unsigned int*)(addr & ~3));
}

static const char* dreg_names[] = {"D0","D1","D2","D3","D4","D5","D6","D7"};
static const char* areg_names[] = {"A0","A1","A2","A3","A4","A5","A6","A7"};
static const char* size_names[] = {".B",".W",".L"};
static const char* cc_names[] = {
    "T","F","HI","LS","CC","CS","NE","EQ",
    "VC","VS","PL","MI","GE","LT","GT","LE"
};

static int decode_ea(unsigned int addr, int mode, int reg, char* buf, int sz) {
    int extra = 0;
    switch (mode) {
        case 0: // Dn
            ksprintf(buf, 32, "%s", dreg_names[reg]);
            break;
        case 1: // An
            ksprintf(buf, 32, "%s", areg_names[reg]);
            break;
        case 2: // (An)
            ksprintf(buf, 32, "(%s)", areg_names[reg]);
            break;
        case 3: // (An)+
            ksprintf(buf, 32, "(%s)+", areg_names[reg]);
            break;
        case 4: // -(An)
            ksprintf(buf, 32, "-(%s)", areg_names[reg]);
            break;
        case 5: { // d(An)
            short disp = (short)mem_read_word(addr);
            ksprintf(buf, 32, "%d(%s)", (int)disp, areg_names[reg]);
            extra = 2;
            break;
        }
        case 7:
            switch (reg) {
                case 0: { // Absolute short
                    unsigned short abs = mem_read_word(addr);
                    ksprintf(buf, 32, "$%04X", abs);
                    extra = 2;
                    break;
                }
                case 1: { // Absolute long
                    unsigned int abs = mem_read_long(addr);
                    ksprintf(buf, 32, "$%08X", abs);
                    extra = 4;
                    break;
                }
                case 4: { // Immediate
                    if (sz == 0) {
                        ksprintf(buf, 32, "#$%02X", mem_read_word(addr) & 0xFF);
                        extra = 2;
                    } else if (sz == 1) {
                        ksprintf(buf, 32, "#$%04X", mem_read_word(addr));
                        extra = 2;
                    } else {
                        ksprintf(buf, 32, "#$%08X", mem_read_long(addr));
                        extra = 4;
                    }
                    break;
                }
                default:
                    ksprintf(buf, 32, "???");
                    break;
            }
            break;
        default:
            ksprintf(buf, 32, "???");
            break;
    }
    return extra;
}

static void disassemble_one(unsigned int addr, DisasmResult* res) {
    unsigned short opcode = mem_read_word(addr);
    int len = 2;
    char ea1[32] = {}, ea2[32] = {};

    // Decode major opcode groups
    int group = (opcode >> 12) & 0xF;

    switch (group) {
        case 0x0: // Immediate ops, bit ops
            if ((opcode & 0xFF00) == 0x0000 && (opcode & 0xC0) != 0xC0) {
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "ORI%s    %s", size_names[sz], ea1);
            } else {
                ksprintf(res->text, 80, "DC.W    $%04X", opcode);
            }
            break;
        case 0x1: case 0x2: case 0x3: { // MOVE
            int sz_code = group;
            int sz = (sz_code == 1) ? 0 : (sz_code == 3) ? 1 : 2;
            int src_mode = (opcode >> 3) & 7;
            int src_reg = opcode & 7;
            int dst_reg = (opcode >> 9) & 7;
            int dst_mode = (opcode >> 6) & 7;
            len += decode_ea(addr + len, src_mode, src_reg, ea1, sz);
            len += decode_ea(addr + len, dst_mode, dst_reg, ea2, sz);
            ksprintf(res->text, 80, "MOVE%s   %s,%s", size_names[sz], ea1, ea2);
            break;
        }
        case 0x4: // Misc
            if (opcode == 0x4E75) {
                ksprintf(res->text, 80, "RTS");
            } else if (opcode == 0x4E71) {
                ksprintf(res->text, 80, "NOP");
            } else if (opcode == 0x4E70) {
                ksprintf(res->text, 80, "RESET");
            } else if ((opcode & 0xFFF8) == 0x4E50) {
                short disp = (short)mem_read_word(addr + 2);
                ksprintf(res->text, 80, "LINK    %s,#%d", areg_names[opcode & 7], (int)disp);
                len = 4;
            } else if ((opcode & 0xFFF8) == 0x4E58) {
                ksprintf(res->text, 80, "UNLK    %s", areg_names[opcode & 7]);
            } else if ((opcode & 0xFFC0) == 0x4EC0) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "JMP     %s", ea1);
            } else if ((opcode & 0xFFC0) == 0x4E80) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "JSR     %s", ea1);
            } else if ((opcode & 0xFF00) == 0x4A00) {
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "TST%s    %s", size_names[sz], ea1);
            } else if ((opcode & 0xFF00) == 0x4200) {
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "CLR%s    %s", size_names[sz], ea1);
            } else if ((opcode & 0xFFF8) == 0x4840) {
                ksprintf(res->text, 80, "SWAP    %s", dreg_names[opcode & 7]);
            } else if ((opcode & 0xFE38) == 0x4800) {
                int opmode = (opcode >> 6) & 7;
                ksprintf(res->text, 80, "EXT%s    %s", (opmode == 3) ? ".L" : ".W", dreg_names[opcode & 7]);
            } else if ((opcode & 0xFB80) == 0x4880) {
                ksprintf(res->text, 80, "MOVEM   ...");
                len += 2; // mask word
            } else if ((opcode & 0xF1C0) == 0x41C0) {
                int areg = (opcode >> 9) & 7;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "LEA     %s,%s", ea1, areg_names[areg]);
            } else if ((opcode & 0xFFC0) == 0x4840) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "PEA     %s", ea1);
            } else {
                ksprintf(res->text, 80, "DC.W    $%04X", opcode);
            }
            break;
        case 0x5: // ADDQ/SUBQ/Scc/DBcc
            if ((opcode & 0xF0C0) == 0x50C0) {
                int cc = (opcode >> 8) & 0xF;
                if ((opcode & 0x38) == 0x08) {
                    short disp = (short)mem_read_word(addr + 2);
                    unsigned int target = addr + 2 + disp;
                    ksprintf(res->text, 80, "DB%s    %s,$%08X", cc_names[cc], dreg_names[opcode & 7], target);
                    len = 4;
                } else {
                    len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 0);
                    ksprintf(res->text, 80, "S%s     %s", cc_names[cc], ea1);
                }
            } else {
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                if (opcode & 0x0100) {
                    ksprintf(res->text, 80, "SUBQ%s   #%d,%s", size_names[sz], data, ea1);
                } else {
                    ksprintf(res->text, 80, "ADDQ%s   #%d,%s", size_names[sz], data, ea1);
                }
            }
            break;
        case 0x6: { // Bcc / BRA / BSR
            int cc = (opcode >> 8) & 0xF;
            int disp = (signed char)(opcode & 0xFF);
            if (disp == 0) {
                disp = (short)mem_read_word(addr + 2);
                len = 4;
            }
            unsigned int target = addr + 2 + disp;
            if (cc == 0) ksprintf(res->text, 80, "BRA     $%08X", target);
            else if (cc == 1) ksprintf(res->text, 80, "BSR     $%08X", target);
            else ksprintf(res->text, 80, "B%s     $%08X", cc_names[cc], target);
            break;
        }
        case 0x7: // MOVEQ
            ksprintf(res->text, 80, "MOVEQ   #%d,%s", (signed char)(opcode & 0xFF), dreg_names[(opcode >> 9) & 7]);
            break;
        case 0x8: // OR / DIV
            if ((opcode & 0xF1C0) == 0x80C0) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 1);
                ksprintf(res->text, 80, "DIVU    %s,%s", ea1, dreg_names[(opcode >> 9) & 7]);
            } else if ((opcode & 0xF1C0) == 0x81C0) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 1);
                ksprintf(res->text, 80, "DIVS    %s,%s", ea1, dreg_names[(opcode >> 9) & 7]);
            } else {
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "OR%s    %s,%s", size_names[sz], ea1, dreg_names[(opcode >> 9) & 7]);
            }
            break;
        case 0x9: { // SUB
            int sz = (opcode >> 6) & 3;
            if (sz == 3) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "SUBA.L  %s,%s", ea1, areg_names[(opcode >> 9) & 7]);
            } else {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "SUB%s    %s,%s", size_names[sz], ea1, dreg_names[(opcode >> 9) & 7]);
            }
            break;
        }
        case 0xB: { // CMP / EOR
            int sz = (opcode >> 6) & 3;
            if ((opcode & 0x0100) && sz < 3) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "EOR%s    %s,%s", size_names[sz], dreg_names[(opcode >> 9) & 7], ea1);
            } else {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "CMP%s    %s,%s", size_names[sz], ea1, dreg_names[(opcode >> 9) & 7]);
            }
            break;
        }
        case 0xC: // AND / MUL
            if ((opcode & 0xF1C0) == 0xC0C0) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 1);
                ksprintf(res->text, 80, "MULU    %s,%s", ea1, dreg_names[(opcode >> 9) & 7]);
            } else if ((opcode & 0xF1C0) == 0xC1C0) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 1);
                ksprintf(res->text, 80, "MULS    %s,%s", ea1, dreg_names[(opcode >> 9) & 7]);
            } else {
                int sz = (opcode >> 6) & 3;
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "AND%s    %s,%s", size_names[sz], ea1, dreg_names[(opcode >> 9) & 7]);
            }
            break;
        case 0xD: { // ADD
            int sz = (opcode >> 6) & 3;
            if (sz == 3) {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, 2);
                ksprintf(res->text, 80, "ADDA.L  %s,%s", ea1, areg_names[(opcode >> 9) & 7]);
            } else {
                len += decode_ea(addr + len, (opcode >> 3) & 7, opcode & 7, ea1, sz);
                ksprintf(res->text, 80, "ADD%s    %s,%s", size_names[sz], ea1, dreg_names[(opcode >> 9) & 7]);
            }
            break;
        }
        case 0xE: { // Shift/rotate
            int dir = (opcode >> 8) & 1;
            int sz = (opcode >> 6) & 3;
            int ir = (opcode >> 5) & 1;
            int type = (opcode >> 3) & 3;
            const char* type_names[] = {"AS","LS","ROX","RO"};
            const char* dir_s = dir ? "L" : "R";
            int count_reg = (opcode >> 9) & 7;
            if (ir) {
                ksprintf(res->text, 80, "%s%s%s    %s,%s", type_names[type], dir_s, size_names[sz],
                        dreg_names[count_reg], dreg_names[opcode & 7]);
            } else {
                int cnt = count_reg; if (cnt == 0) cnt = 8;
                ksprintf(res->text, 80, "%s%s%s    #%d,%s", type_names[type], dir_s, size_names[sz],
                        cnt, dreg_names[opcode & 7]);
            }
            break;
        }
        default:
            ksprintf(res->text, 80, "DC.W    $%04X", opcode);
            break;
    }

    res->length = len;
}

// --- UI Drawing ---

static void draw_header() {
    neo::display::set_cursor(0, 0);
    neo::display::set_color(0, 6); // Black on cyan
    int w = neo::display::get_width();
    neo::display::printf(" NeoDebug v1.0 - 68K Visual Debugger");
    for (int i = 36; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

static void draw_registers() {
    neo::display::set_cursor(0, 2);
    neo::display::set_fg(3); // Cyan
    neo::display::printf("+---- Registers --------------------------------+\n");
    neo::display::set_fg(7);

    for (int i = 0; i < 8; i++) {
        neo::display::set_fg(6); // Yellow label
        neo::display::printf(" D%d", i);
        neo::display::set_fg(7);
        neo::display::printf("=%08X", dbg->regs.d[i]);
        neo::display::set_fg(6);
        neo::display::printf("  A%d", i);
        neo::display::set_fg(7);
        neo::display::printf("=%08X\n", dbg->regs.a[i]);
    }

    neo::display::set_fg(5); // Magenta
    neo::display::printf(" PC");
    neo::display::set_fg(7);
    neo::display::printf("=%08X", dbg->regs.pc);
    neo::display::set_fg(5);
    neo::display::printf("  SR");
    neo::display::set_fg(7);
    neo::display::printf("=%04X ", dbg->regs.sr);

    // Decode SR flags
    neo::display::set_fg(3);
    neo::display::printf("[");
    unsigned short sr = dbg->regs.sr;
    neo::display::printf("%c", (sr & 0x10) ? 'X' : '-');
    neo::display::printf("%c", (sr & 0x08) ? 'N' : '-');
    neo::display::printf("%c", (sr & 0x04) ? 'Z' : '-');
    neo::display::printf("%c", (sr & 0x02) ? 'V' : '-');
    neo::display::printf("%c", (sr & 0x01) ? 'C' : '-');
    neo::display::printf("]\n");
    neo::display::set_fg(7);

    neo::display::set_fg(3);
    neo::display::printf("+-----------------------------------------------+\n");
    neo::display::set_fg(7);
}

static void draw_memory_dump(unsigned int addr, int rows) {
    neo::display::set_fg(3);
    neo::display::printf("+---- Memory at $%08X ----------------------+\n", addr);
    neo::display::set_fg(7);

    for (int r = 0; r < rows; r++) {
        unsigned int row_addr = addr + r * 16;
        neo::display::set_fg(6);
        neo::display::printf(" %08X: ", row_addr);
        neo::display::set_fg(7);

        // Hex bytes
        for (int c = 0; c < 16; c++) {
            unsigned char b = mem_read_byte(row_addr + c);
            neo::display::printf("%02X ", b);
            if (c == 7) neo::display::putchar(' ');
        }

        // ASCII
        neo::display::set_fg(2);
        neo::display::printf(" |");
        for (int c = 0; c < 16; c++) {
            unsigned char b = mem_read_byte(row_addr + c);
            neo::display::putchar((b >= 0x20 && b <= 0x7E) ? b : '.');
        }
        neo::display::printf("|\n");
        neo::display::set_fg(7);
    }
}

static void draw_disassembly(unsigned int addr, int lines) {
    neo::display::set_fg(3);
    neo::display::printf("+---- Disassembly at $%08X -----------------+\n", addr);
    neo::display::set_fg(7);

    unsigned int cur = addr;
    for (int i = 0; i < lines; i++) {
        DisasmResult res;
        disassemble_one(cur, &res);

        // Check for breakpoint
        bool is_bp = false;
        for (int b = 0; b < dbg->num_breakpoints; b++) {
            if (dbg->breakpoints[b].active && dbg->breakpoints[b].address == cur) {
                is_bp = true;
                break;
            }
        }

        bool is_pc = (cur == dbg->regs.pc);

        if (is_pc) neo::display::set_fg(2); // Green for PC
        else if (is_bp) neo::display::set_fg(1); // Red for breakpoint
        else neo::display::set_fg(6);

        neo::display::printf(" %s%08X: ", is_pc ? ">" : (is_bp ? "*" : " "), cur);
        neo::display::set_fg(7);

        // Raw bytes
        for (int b = 0; b < res.length && b < 6; b += 2) {
            neo::display::printf("%04X ", mem_read_word(cur + b));
        }
        for (int b = res.length; b < 6; b += 2) {
            neo::display::printf("     ");
        }

        neo::display::printf(" %s\n", res.text);
        cur += res.length;
    }
}

static void draw_watches() {
    if (dbg->num_watches == 0) return;

    neo::display::set_fg(3);
    neo::display::printf("+---- Watch Expressions ------------------------+\n");
    neo::display::set_fg(7);

    for (int i = 0; i < dbg->num_watches; i++) {
        Watch& w = dbg->watches[i];
        if (!w.active) continue;

        unsigned int val = 0;
        if (w.size == 1) val = mem_read_byte(w.address);
        else if (w.size == 2) val = mem_read_word(w.address);
        else val = mem_read_long(w.address);

        bool changed = (val != w.last_value);
        if (changed) neo::display::set_fg(1); // Red for changed
        else neo::display::set_fg(7);

        neo::display::printf(" [%d] %-16s $%08X = ", i, w.name, w.address);
        if (w.size == 1) neo::display::printf("$%02X", val);
        else if (w.size == 2) neo::display::printf("$%04X", val);
        else neo::display::printf("$%08X", val);
        if (changed) neo::display::printf(" (was $%08X)", w.last_value);
        neo::display::printf("\n");

        w.last_value = val;
    }
    neo::display::set_fg(7);
}

static void draw_breakpoints() {
    neo::display::set_fg(3);
    neo::display::printf("+---- Breakpoints ------------------------------+\n");
    neo::display::set_fg(7);

    if (dbg->num_breakpoints == 0) {
        neo::display::printf(" (no breakpoints set)\n");
        return;
    }

    for (int i = 0; i < dbg->num_breakpoints; i++) {
        Breakpoint& bp = dbg->breakpoints[i];
        if (!bp.active) continue;
        neo::display::printf(" [%d] $%08X %s hits=%d %s\n",
            i, bp.address,
            bp.enabled ? "ON " : "OFF",
            bp.hit_count,
            bp.label);
    }
}

static void draw_callstack() {
    neo::display::set_fg(3);
    neo::display::printf("+---- Call Stack --------------------------------+\n");
    neo::display::set_fg(7);

    if (dbg->callstack_depth == 0) {
        neo::display::printf(" (empty)\n");
        return;
    }

    for (int i = dbg->callstack_depth - 1; i >= 0; i--) {
        neo::display::printf(" %2d: $%08X -> $%08X\n",
            i, dbg->callstack[i].from_addr, dbg->callstack[i].to_addr);
    }
}

static void search_memory(unsigned int start, unsigned int end, const unsigned char* pattern, int pat_len) {
    int found = 0;
    neo::display::printf("Searching $%08X - $%08X for %d byte pattern...\n", start, end, pat_len);

    for (unsigned int addr = start; addr <= end - (unsigned int)pat_len; addr++) {
        bool match = true;
        for (int i = 0; i < pat_len; i++) {
            if (mem_read_byte(addr + i) != pattern[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            neo::display::set_fg(2);
            neo::display::printf("  Found at $%08X\n", addr);
            neo::display::set_fg(7);
            found++;
            if (found >= 100) {
                neo::display::printf("  (stopped after 100 matches)\n");
                break;
            }
        }
    }

    if (found == 0) neo::display::printf("  Pattern not found.\n");
    else neo::display::printf("  %d match(es) found.\n", found);
}

static void full_view() {
    neo::display::clear();
    draw_header();
    draw_registers();
    draw_disassembly(dbg->disasm_addr, 8);
    draw_memory_dump(dbg->mem_view_addr, 6);
    draw_watches();
    draw_breakpoints();
    draw_callstack();
    neo::display::printf("\n");
}

static void show_help() {
    neo::display::printf("\nNeoDebug Commands:\n");
    neo::display::printf("  m <addr>          - Memory dump at address\n");
    neo::display::printf("  d <addr>          - Disassemble at address\n");
    neo::display::printf("  r                 - Show registers\n");
    neo::display::printf("  sr <reg> <val>    - Set register (d0-d7, a0-a7, pc, sr)\n");
    neo::display::printf("  bp <addr> [name]  - Add breakpoint\n");
    neo::display::printf("  bd <num>          - Delete breakpoint\n");
    neo::display::printf("  be <num>          - Enable/disable breakpoint\n");
    neo::display::printf("  bl                - List breakpoints\n");
    neo::display::printf("  w <addr> <sz> [name] - Add watch (sz=1/2/4)\n");
    neo::display::printf("  wd <num>          - Delete watch\n");
    neo::display::printf("  s                 - Single step\n");
    neo::display::printf("  g [addr]          - Go (continue) from PC or addr\n");
    neo::display::printf("  sb <start> <end> <hex..> - Search memory\n");
    neo::display::printf("  f                 - Full view (all panels)\n");
    neo::display::printf("  wb <addr> <val>   - Write byte\n");
    neo::display::printf("  ww <addr> <val>   - Write word\n");
    neo::display::printf("  wl <addr> <val>   - Write long\n");
    neo::display::printf("  cs                - Show call stack\n");
    neo::display::printf("  h                 - Help\n");
    neo::display::printf("  q                 - Quit\n\n");
}

static void command_loop() {
    char input[INODE_SIZE];

    full_view();

    while (true) {
        neo::display::set_fg(3);
        neo::console::getline(input, INODE_SIZE, "dbg> ");
        neo::display::set_fg(7);

        if (input[0] == 0) continue;
        neo::console::history_add(input);

        const char* p = skip_ws(input);
        char cmd = to_upper(*p);
        p++;

        if (cmd == 'Q') break;

        if (cmd == 'H') {
            show_help();
        } else if (cmd == 'F') {
            full_view();
        } else if (cmd == 'R') {
            draw_registers();
        } else if (cmd == 'M') {
            p = skip_ws(p);
            if (*p) dbg->mem_view_addr = parse_hex(p);
            draw_memory_dump(dbg->mem_view_addr, 16);
            dbg->mem_view_addr += INODE_SIZE;
        } else if (cmd == 'D') {
            p = skip_ws(p);
            if (*p) dbg->disasm_addr = parse_hex(p);
            draw_disassembly(dbg->disasm_addr, 16);
        } else if (cmd == 'S' && to_upper(*p) == 'R') {
            // Set register
            p++;
            p = skip_ws(p);
            char regname[8];
            int ri = 0;
            while (*p && !is_space(*p) && ri < 7) regname[ri++] = to_upper(*p++);
            regname[ri] = 0;
            p = skip_ws(p);
            unsigned int val = parse_hex(p);

            if (regname[0] == 'D' && regname[1] >= '0' && regname[1] <= '7') {
                dbg->regs.d[regname[1] - '0'] = val;
                neo::display::printf("D%c = $%08X\n", regname[1], val);
            } else if (regname[0] == 'A' && regname[1] >= '0' && regname[1] <= '7') {
                dbg->regs.a[regname[1] - '0'] = val;
                neo::display::printf("A%c = $%08X\n", regname[1], val);
            } else if (neo_strcmp(regname, "PC") == 0) {
                dbg->regs.pc = val;
                dbg->disasm_addr = val;
                neo::display::printf("PC = $%08X\n", val);
            } else if (neo_strcmp(regname, "SR") == 0) {
                dbg->regs.sr = val & 0xFFFF;
                neo::display::printf("SR = $%04X\n", dbg->regs.sr);
            } else {
                neo::display::printf("Unknown register: %s\n", regname);
            }
        } else if (cmd == 'S' && to_upper(*p) == 'B') {
            // Search bytes
            p++;
            p = skip_ws(p);
            unsigned int start = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);
            unsigned int end = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);

            unsigned char pattern[MAX_SEARCH_LEN];
            int pat_len = 0;
            while (*p && pat_len < MAX_SEARCH_LEN) {
                p = skip_ws(p);
                if (!is_hex(*p)) break;
                int hi = is_digit(*p) ? (*p - '0') : (to_upper(*p) - 'A' + 10);
                p++;
                int lo = 0;
                if (is_hex(*p)) {
                    lo = is_digit(*p) ? (*p - '0') : (to_upper(*p) - 'A' + 10);
                    p++;
                }
                pattern[pat_len++] = (hi << 4) | lo;
            }

            if (pat_len > 0) search_memory(start, end, pattern, pat_len);
            else neo::display::printf("No pattern specified.\n");
        } else if (cmd == 'S') {
            // Single step simulation
            DisasmResult res;
            disassemble_one(dbg->regs.pc, &res);
            neo::display::set_fg(2);
            neo::display::printf("  Step: $%08X  %s\n", dbg->regs.pc, res.text);
            neo::display::set_fg(7);
            dbg->regs.pc += res.length;
            dbg->disasm_addr = dbg->regs.pc;
        } else if (cmd == 'G') {
            p = skip_ws(p);
            if (*p) dbg->regs.pc = parse_hex(p);
            neo::display::printf("Running from $%08X (simulation)...\n", dbg->regs.pc);
            // Simulated execution: step through until breakpoint or RTS
            for (int steps = 0; steps < 1000; steps++) {
                // Check breakpoints
                for (int b = 0; b < dbg->num_breakpoints; b++) {
                    if (dbg->breakpoints[b].active && dbg->breakpoints[b].enabled &&
                        dbg->breakpoints[b].address == dbg->regs.pc) {
                        dbg->breakpoints[b].hit_count++;
                        neo::display::set_fg(1);
                        neo::display::printf("Breakpoint %d hit at $%08X (%s)\n",
                            b, dbg->regs.pc, dbg->breakpoints[b].label);
                        neo::display::set_fg(7);
                        goto done_run;
                    }
                }
                unsigned short opcode = mem_read_word(dbg->regs.pc);
                if (opcode == 0x4E75) { // RTS
                    neo::display::printf("RTS reached at $%08X\n", dbg->regs.pc);
                    break;
                }
                DisasmResult res;
                disassemble_one(dbg->regs.pc, &res);
                dbg->regs.pc += res.length;
            }
            done_run:
            dbg->disasm_addr = dbg->regs.pc;
        } else if (cmd == 'B' && to_upper(*p) == 'P') {
            // Add breakpoint
            p++;
            p = skip_ws(p);
            unsigned int addr = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);

            if (dbg->num_breakpoints < MAX_BREAKPOINTS) {
                Breakpoint& bp = dbg->breakpoints[dbg->num_breakpoints];
                bp.address = addr;
                bp.enabled = true;
                bp.active = true;
                bp.hit_count = 0;
                neo_strncpy(bp.label, (*p) ? p : "", 31);
                bp.label[31] = 0;
                neo::display::printf("Breakpoint %d set at $%08X\n", dbg->num_breakpoints, addr);
                dbg->num_breakpoints++;
            }
        } else if (cmd == 'B' && to_upper(*p) == 'D') {
            p++;
            p = skip_ws(p);
            int idx = 0;
            while (is_digit(*p)) { idx = idx * 10 + (*p - '0'); p++; }
            if (idx >= 0 && idx < dbg->num_breakpoints) {
                dbg->breakpoints[idx].active = false;
                neo::display::printf("Breakpoint %d deleted.\n", idx);
            }
        } else if (cmd == 'B' && to_upper(*p) == 'E') {
            p++;
            p = skip_ws(p);
            int idx = 0;
            while (is_digit(*p)) { idx = idx * 10 + (*p - '0'); p++; }
            if (idx >= 0 && idx < dbg->num_breakpoints) {
                dbg->breakpoints[idx].enabled = !dbg->breakpoints[idx].enabled;
                neo::display::printf("Breakpoint %d %s.\n", idx,
                    dbg->breakpoints[idx].enabled ? "enabled" : "disabled");
            }
        } else if (cmd == 'B' && to_upper(*p) == 'L') {
            draw_breakpoints();
        } else if (cmd == 'W' && to_upper(*p) == 'B') {
            // Write byte
            p++;
            p = skip_ws(p);
            unsigned int addr = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);
            unsigned int val = parse_hex(p);
            *((volatile unsigned char*)addr) = (unsigned char)val;
            neo::display::printf("Wrote $%02X to $%08X\n", val & 0xFF, addr);
        } else if (cmd == 'W' && to_upper(*p) == 'W') {
            // Write word
            p++;
            p = skip_ws(p);
            unsigned int addr = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);
            unsigned int val = parse_hex(p);
            *((volatile unsigned short*)(addr & ~1)) = (unsigned short)val;
            neo::display::printf("Wrote $%04X to $%08X\n", val & 0xFFFF, addr);
        } else if (cmd == 'W' && to_upper(*p) == 'L') {
            // Write long
            p++;
            p = skip_ws(p);
            unsigned int addr = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);
            unsigned int val = parse_hex(p);
            *((volatile unsigned int*)(addr & ~3)) = val;
            neo::display::printf("Wrote $%08X to $%08X\n", val, addr);
        } else if (cmd == 'W' && to_upper(*p) == 'D') {
            // Delete watch
            p++;
            p = skip_ws(p);
            int idx = 0;
            while (is_digit(*p)) { idx = idx * 10 + (*p - '0'); p++; }
            if (idx >= 0 && idx < dbg->num_watches) {
                dbg->watches[idx].active = false;
                neo::display::printf("Watch %d deleted.\n", idx);
            }
        } else if (cmd == 'W') {
            // Add watch: w <addr> <size> [name]
            p = skip_ws(p);
            unsigned int addr = parse_hex(p);
            while (*p && !is_space(*p)) p++;
            p = skip_ws(p);
            int sz = 4;
            if (is_digit(*p)) { sz = *p - '0'; p++; }
            p = skip_ws(p);

            if (dbg->num_watches < MAX_WATCHES) {
                Watch& w = dbg->watches[dbg->num_watches];
                w.address = addr;
                w.size = sz;
                w.active = true;
                w.last_value = 0;
                neo_strncpy(w.name, (*p) ? p : "watch", 31);
                w.name[31] = 0;
                neo::display::printf("Watch %d: $%08X (%d bytes)\n", dbg->num_watches, addr, sz);
                dbg->num_watches++;
            }
        } else if (cmd == 'C' && to_upper(*p) == 'S') {
            draw_callstack();
        } else {
            neo::display::printf("Unknown command. Type 'h' for help.\n");
        }
    }
}

} // namespace neodebug

extern "C" void app_main(int argc, char** argv) {
    neodebug::dbg = (neodebug::DebugState*)neo::mem::alloc(sizeof(neodebug::DebugState));
    neo_memset(neodebug::dbg, 0, sizeof(neodebug::DebugState));

    neodebug::dbg->mem_view_addr = 0x00000000;
    neodebug::dbg->disasm_addr = 0x00000000;
    neodebug::dbg->mem_view_cols = 16;
    neodebug::dbg->mem_view_rows = 8;

    // Parse args
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '$' || (argv[i][0] >= '0' && argv[i][0] <= '9')) {
            unsigned int addr = neodebug::parse_hex(argv[i]);
            neodebug::dbg->regs.pc = addr;
            neodebug::dbg->disasm_addr = addr;
            neodebug::dbg->mem_view_addr = addr;
        }
    }

    neodebug::show_help();
    neodebug::command_loop();

    neo::mem::free(neodebug::dbg);
}
