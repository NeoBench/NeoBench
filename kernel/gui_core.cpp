// gui_core.cpp - NeoBench GUI Core Implementation
// Bare-metal Amiga 68030/040/060 - ECS/AGA/RTG graphics
#include "gui.h"
#include "neobench.h"

namespace {

// ============================================================
// Fixed-point math (16.16)
// ============================================================
typedef int32 fixed16;
static constexpr int32 FP_SHIFT = 16;
static constexpr int32 FP_ONE = 1 << FP_SHIFT;
static constexpr int32 FP_HALF = FP_ONE >> 1;

static inline fixed16 int_to_fp(int32 v) { return v << FP_SHIFT; }
static inline int32 fp_to_int(fixed16 v) { return v >> FP_SHIFT; }
static inline fixed16 fp_mul(fixed16 a, fixed16 b) {
    return (int32)(((int64)a * (int64)b) >> FP_SHIFT);
}
static inline fixed16 fp_div(fixed16 a, fixed16 b) {
    if (b == 0) return 0;
    return (int32)(((int64)a << FP_SHIFT) / b);
}

// Sine table (INODE_SIZE entries, values in 16.16 fixed point for 0..2*PI)
// sin(i * 2*PI / INODE_SIZE)
static const int32 sine_table[INODE_SIZE] = {
    0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
    12540, 14010, 15447, 16846, 18205, 19520, 20788, 22006,
    23170, 24279, 25330, 26320, 27246, 28106, 28899, 29622,
    30274, 30853, 31357, 31786, 32138, 32413, 32610, 32729,
    32768, 32729, 32610, 32413, 32138, 31786, 31357, 30853,
    30274, 29622, 28899, 28106, 27246, 26320, 25330, 24279,
    23170, 22006, 20788, 19520, 18205, 16846, 15447, 14010,
    12540, 11039, 9512, 7962, 6393, 4808, 3212, 1608,
    0, -1608, -3212, -4808, -6393, -7962, -9512, -11039,
    -12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
    -23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
    -30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729,
    -32768, -32729, -32610, -32413, -32138, -31786, -31357, -30853,
    -30274, -29622, -28899, -28106, -27246, -26320, -25330, -24279,
    -23170, -22006, -20788, -19520, -18205, -16846, -15447, -14010,
    -12540, -11039, -9512, -7962, -6393, -4808, -3212, -1608,
    0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
    12540, 14010, 15447, 16846, 18205, 19520, 20788, 22006,
    23170, 24279, 25330, 26320, 27246, 28106, 28899, 29622,
    30274, 30853, 31357, 31786, 32138, 32413, 32610, 32729,
    32768, 32729, 32610, 32413, 32138, 31786, 31357, 30853,
    30274, 29622, 28899, 28106, 27246, 26320, 25330, 24279,
    23170, 22006, 20788, 19520, 18205, 16846, 15447, 14010,
    12540, 11039, 9512, 7962, 6393, 4808, 3212, 1608,
    0, -1608, -3212, -4808, -6393, -7962, -9512, -11039,
    -12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
    -23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
    -30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729,
    -32768, -32729, -32610, -32413, -32138, -31786, -31357, -30853,
    -30274, -29622, -28899, -28106, -27246, -26320, -25330, -24279,
    -23170, -22006, -20788, -19520, -18205, -16846, -15447, -14010,
    -12540, -11039, -9512, -7962, -6393, -4808, -3212, -1608
};

// Fixed-point sine: input is 0..255 representing 0..2*PI
// Returns fixed-point 16.16 in range [-32768..32768] (i.e. [-0.5..0.5] * 65536)
static inline int32 fp_sin(uint32 angle) {
    return sine_table[angle & 0xFF];
}
static inline int32 fp_cos(uint32 angle) {
    return sine_table[(angle + 64) & 0xFF];
}

// Integer square root
static uint32 isqrt(uint32 n) {
    if (n == 0) return 0;
    uint32 x = n;
    uint32 y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

// Simple pseudo-random number generator
static uint32 rng_state = 12345;
static uint32 rng_next() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static inline uint32 min_u32(uint32 a, uint32 b) { return a < b ? a : b; }
static inline uint32 max_u32(uint32 a, uint32 b) { return a > b ? a : b; }
static inline int32 min_i32(int32 a, int32 b) { return a < b ? a : b; }
static inline int32 max_i32(int32 a, int32 b) { return a > b ? a : b; }
static inline int32 clamp_i32(int32 v, int32 lo, int32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static inline uint8 clamp_u8(int32 v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8)v;
}

// ============================================================
// Font Data
// ============================================================

static const uint8 font_8x16[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ' ' (32)
    0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, // '!' (33)
    0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '"' (34)
    0x00, 0x00, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, // '#' (35)
    0x00, 0x10, 0x7C, 0xD6, 0xD0, 0x7C, 0x16, 0xD6, 0x7C, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '$' (36)
    0x00, 0x00, 0xC6, 0xC6, 0x0C, 0x18, 0x30, 0x60, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '%' (37)
    0x00, 0x00, 0x38, 0x6C, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '&' (38)
    0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ''' (39)
    0x00, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, // '(' (40)
    0x00, 0x60, 0x30, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, // ')' (41)
    0x00, 0x00, 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '*' (42)
    0x00, 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '+' (43)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, // ',' (44)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '-' (45)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '.' (46)
    0x00, 0x02, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '/' (47)
    0x00, 0x00, 0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '0' (48)
    0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '1' (49)
    0x00, 0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '2' (50)
    0x00, 0x00, 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '3' (51)
    0x00, 0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '4' (52)
    0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '5' (53)
    0x00, 0x00, 0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '6' (54)
    0x00, 0x00, 0xFE, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '7' (55)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '8' (56)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '9' (57)
    0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ':' (58)
    0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ';' (59)
    0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '<' (60)
    0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '=' (61)
    0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '>' (62)
    0x00, 0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '?' (63)
    0x00, 0x00, 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '@' (64)
    0x00, 0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'A' (65)
    0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'B' (66)
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'C' (67)
    0x00, 0x00, 0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'D' (68)
    0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'E' (69)
    0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'F' (70)
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xCE, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'G' (71)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'H' (72)
    0x00, 0x00, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'I' (73)
    0x00, 0x00, 0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'J' (74)
    0x00, 0x00, 0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'K' (75)
    0x00, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'L' (76)
    0x00, 0x00, 0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'M' (77)
    0x00, 0x00, 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'N' (78)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'O' (79)
    0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'P' (80)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, // 'Q' (81)
    0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'R' (82)
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'S' (83)
    0x00, 0x00, 0xFE, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'T' (84)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'U' (85)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x6C, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'V' (86)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'W' (87)
    0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'X' (88)
    0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'Y' (89)
    0x00, 0x00, 0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'Z' (90)
    0x00, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '[' (91)
    0x00, 0x80, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '\' (92)
    0x00, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ']' (93)
    0x00, 0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '^' (94)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, // '_' (95)
    0x00, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '`' (96)
    0x00, 0x00, 0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0xC6, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'a' (97)
    0x00, 0x00, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'b' (98)
    0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'c' (99)
    0x00, 0x00, 0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'd' (100)
    0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'e' (101)
    0x00, 0x00, 0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'f' (102)
    0x00, 0x00, 0x00, 0x00, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, // 'g' (103)
    0x00, 0x00, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'h' (104)
    0x00, 0x00, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'i' (105)
    0x00, 0x00, 0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, 0x78, 0x00, 0x00, 0x00, 0x00, // 'j' (106)
    0x00, 0x00, 0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'k' (107)
    0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'l' (108)
    0x00, 0x00, 0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'm' (109)
    0x00, 0x00, 0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'n' (110)
    0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'o' (111)
    0x00, 0x00, 0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00, // 'p' (112)
    0x00, 0x00, 0x00, 0x00, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, // 'q' (113)
    0x00, 0x00, 0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'r' (114)
    0x00, 0x00, 0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0x06, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 's' (115)
    0x00, 0x00, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 't' (116)
    0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'u' (117)
    0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'v' (118)
    0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'w' (119)
    0x00, 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'x' (120)
    0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00, // 'y' (121)
    0x00, 0x00, 0x00, 0x00, 0xFE, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'z' (122)
    0x00, 0x0E, 0x18, 0x18, 0x18, 0x70, 0x18, 0x18, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '{' (123)
    0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '|' (124)
    0x00, 0x70, 0x18, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '}' (125)
    0x00, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '~' (126)
};

static const uint8 font_6x10[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ' ' (32)
    0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, // '!' (33)
    0x00, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '"' (34)
    0x00, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, // '#' (35)
    0x00, 0x7C, 0xD6, 0xD0, 0x7C, 0x16, 0xD6, 0x7C, 0x10, 0x00, // '$' (36)
    0x00, 0xC6, 0xC6, 0x0C, 0x18, 0x30, 0x60, 0xC6, 0xC6, 0x00, // '%' (37)
    0x00, 0x38, 0x6C, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00, // '&' (38)
    0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ''' (39)
    0x00, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x06, // '(' (40)
    0x00, 0x30, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x30, 0x60, // ')' (41)
    0x00, 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00, 0x00, // '*' (42)
    0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00, // '+' (43)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, // ',' (44)
    0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, // '-' (45)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, // '.' (46)
    0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00, 0x00, // '/' (47)
    0x00, 0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00, // '0' (48)
    0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, // '1' (49)
    0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00, // '2' (50)
    0x00, 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0x06, 0xC6, 0x7C, 0x00, // '3' (51)
    0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00, // '4' (52)
    0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00, // '5' (53)
    0x00, 0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, // '6' (54)
    0x00, 0xFE, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30, 0x00, // '7' (55)
    0x00, 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, // '8' (56)
    0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00, // '9' (57)
    0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, // ':' (58)
    0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, // ';' (59)
    0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, // '<' (60)
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, // '=' (61)
    0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, // '>' (62)
    0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00, // '?' (63)
    0x00, 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7E, 0x00, 0x00, // '@' (64)
    0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00, // 'A' (65)
    0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00, // 'B' (66)
    0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, // 'C' (67)
    0x00, 0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00, // 'D' (68)
    0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, // 'E' (69)
    0x00, 0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, // 'F' (70)
    0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xCE, 0xC6, 0xC6, 0x7C, 0x00, // 'G' (71)
    0x00, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, // 'H' (72)
    0x00, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, // 'I' (73)
    0x00, 0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00, // 'J' (74)
    0x00, 0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0xC6, 0x00, // 'K' (75)
    0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, // 'L' (76)
    0x00, 0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, // 'M' (77)
    0x00, 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0xC6, 0x00, // 'N' (78)
    0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, // 'O' (79)
    0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, // 'P' (80)
    0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x0E, // 'Q' (81)
    0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0xC6, 0x00, // 'R' (82)
    0x00, 0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0x06, 0xC6, 0x7C, 0x00, // 'S' (83)
    0x00, 0xFE, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, // 'T' (84)
    0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, // 'U' (85)
    0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x6C, 0x38, 0x10, 0x00, // 'V' (86)
    0x00, 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0xC6, 0x00, // 'W' (87)
    0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x00, // 'X' (88)
    0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x18, 0x18, 0x18, 0x00, // 'Y' (89)
    0x00, 0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFE, 0x00, // 'Z' (90)
    0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00, // '[' (91)
    0x00, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00, 0x00, // '\' (92)
    0x00, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00, // ']' (93)
    0x00, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '^' (94)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, // '_' (95)
    0x00, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '`' (96)
    0x00, 0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0xC6, 0x7E, 0x00, // 'a' (97)
    0x00, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xFC, 0x00, // 'b' (98)
    0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, // 'c' (99)
    0x00, 0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00, // 'd' (100)
    0x00, 0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0xC6, 0x7C, 0x00, // 'e' (101)
    0x00, 0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00, // 'f' (102)
    0x00, 0x00, 0x00, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, // 'g' (103)
    0x00, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, // 'h' (104)
    0x00, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, // 'i' (105)
    0x00, 0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, // 'j' (106)
    0x00, 0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00, // 'k' (107)
    0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, // 'l' (108)
    0x00, 0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0xC6, 0x00, // 'm' (109)
    0x00, 0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, // 'n' (110)
    0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, // 'o' (111)
    0x00, 0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, // 'p' (112)
    0x00, 0x00, 0x00, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x06, // 'q' (113)
    0x00, 0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, // 'r' (114)
    0x00, 0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0x06, 0x7C, 0x00, // 's' (115)
    0x00, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x1C, 0x00, // 't' (116)
    0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00, // 'u' (117)
    0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00, // 'v' (118)
    0x00, 0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00, // 'w' (119)
    0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00, // 'x' (120)
    0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, // 'y' (121)
    0x00, 0x00, 0x00, 0xFE, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00, // 'z' (122)
    0x00, 0x18, 0x18, 0x18, 0x70, 0x18, 0x18, 0x18, 0x0E, 0x00, // '{' (123)
    0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, // '|' (124)
    0x00, 0x18, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x18, 0x70, 0x00, // '}' (125)
    0x00, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // '~' (126)
};

static const uint8 font_prop_widths[] = {
    4, // ' ' (32)
    4, // '!' (33)
    6, // '"' (34)
    6, // '#' (35)
    6, // '$' (36)
    6, // '%' (37)
    6, // '&' (38)
    6, // ''' (39)
    5, // '(' (40)
    5, // ')' (41)
    6, // '*' (42)
    6, // '+' (43)
    4, // ',' (44)
    6, // '-' (45)
    4, // '.' (46)
    6, // '/' (47)
    6, // '0' (48)
    6, // '1' (49)
    6, // '2' (50)
    6, // '3' (51)
    6, // '4' (52)
    6, // '5' (53)
    6, // '6' (54)
    6, // '7' (55)
    6, // '8' (56)
    6, // '9' (57)
    4, // ':' (58)
    4, // ';' (59)
    6, // '<' (60)
    6, // '=' (61)
    6, // '>' (62)
    6, // '?' (63)
    8, // '@' (64)
    7, // 'A' (65)
    7, // 'B' (66)
    7, // 'C' (67)
    7, // 'D' (68)
    7, // 'E' (69)
    7, // 'F' (70)
    7, // 'G' (71)
    7, // 'H' (72)
    4, // 'I' (73)
    7, // 'J' (74)
    7, // 'K' (75)
    7, // 'L' (76)
    8, // 'M' (77)
    7, // 'N' (78)
    7, // 'O' (79)
    7, // 'P' (80)
    7, // 'Q' (81)
    7, // 'R' (82)
    7, // 'S' (83)
    7, // 'T' (84)
    7, // 'U' (85)
    7, // 'V' (86)
    8, // 'W' (87)
    7, // 'X' (88)
    7, // 'Y' (89)
    7, // 'Z' (90)
    5, // '[' (91)
    6, // '\' (92)
    5, // ']' (93)
    6, // '^' (94)
    6, // '_' (95)
    6, // '`' (96)
    6, // 'a' (97)
    6, // 'b' (98)
    6, // 'c' (99)
    6, // 'd' (100)
    6, // 'e' (101)
    5, // 'f' (102)
    6, // 'g' (103)
    6, // 'h' (104)
    4, // 'i' (105)
    5, // 'j' (106)
    6, // 'k' (107)
    4, // 'l' (108)
    8, // 'm' (109)
    6, // 'n' (110)
    6, // 'o' (111)
    6, // 'p' (112)
    6, // 'q' (113)
    5, // 'r' (114)
    6, // 's' (115)
    5, // 't' (116)
    6, // 'u' (117)
    6, // 'v' (118)
    8, // 'w' (119)
    6, // 'x' (120)
    6, // 'y' (121)
    6, // 'z' (122)
    5, // '{' (123)
    4, // '|' (124)
    5, // '}' (125)
    6, // '~' (126)
};



// ============================================================
// Mouse cursor bitmaps
// ============================================================
// 16x16 arrow cursor
static const uint16 cursor_arrow[16] = {
    0x8000, 0xC000, 0xE000, 0xF000,
    0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFE00, 0xEF00,
    0xCF00, 0x8780, 0x0780, 0x0300
};
static const uint16 cursor_arrow_mask[16] = {
    0xC000, 0xE000, 0xF000, 0xF800,
    0xFC00, 0xFE00, 0xFF00, 0xFF80,
    0xFFC0, 0xFFE0, 0xFFF0, 0xFF80,
    0xEF80, 0xCFC0, 0x8FC0, 0x07C0
};

// 16x16 hand cursor
static const uint16 cursor_hand[16] = {
    0x0180, 0x03C0, 0x03C0, 0x03C0,
    0x03C0, 0x6FD0, 0x7FF8, 0x7FF8,
    0x3FF8, 0x1FF0, 0x0FF0, 0x07E0,
    0x07E0, 0x03C0, 0x03C0, 0x0180
};
static const uint16 cursor_hand_mask[16] = {
    0x03C0, 0x07E0, 0x07E0, 0x07E0,
    0x77E0, 0xFFF8, 0xFFFC, 0xFFFC,
    0x7FFC, 0x3FF8, 0x1FF8, 0x0FF0,
    0x0FF0, 0x07E0, 0x07E0, 0x03C0
};

// 16x16 resize cursor
static const uint16 cursor_resize[16] = {
    0x0000, 0x7FFE, 0x7FFE, 0x600E,
    0x601E, 0x603E, 0x607E, 0x60FE,
    0x61FE, 0x7FFE, 0x7FFE, 0x007E,
    0x003E, 0x001E, 0x000E, 0x0000
};
static const uint16 cursor_resize_mask[16] = {
    0xFFFF, 0xFFFF, 0xFFFF, 0xF01F,
    0xF03F, 0xF07F, 0xF0FF, 0xF1FF,
    0xF3FF, 0xFFFF, 0xFFFF, 0x00FF,
    0x007F, 0x003F, 0x001F, 0x000F
};

// ============================================================
// Icon bitmaps (32x32, 1-bit, 32 uint32 rows each)
// ============================================================

// Gear icon (system)
static const uint32 icon_gear[32] = {
    0x00000000, 0x00000000, 0x01C38000, 0x03E7C000,
    0x03E7C000, 0x01FBE000, 0x0FFFF000, 0x1FFFF800,
    0x1F01F800, 0x3E00FC00, 0x3C007C00, 0x7C003E00,
    0x7C003E00, 0xFC003F00, 0xFC003F00, 0xFC003F00,
    0xFC003F00, 0xFC003F00, 0x7C003E00, 0x7C003E00,
    0x3C007C00, 0x3E00FC00, 0x1F01F800, 0x1FFFF800,
    0x0FFFF000, 0x01FBE000, 0x03E7C000, 0x03E7C000,
    0x01C38000, 0x00000000, 0x00000000, 0x00000000
};

// Document icon (productivity)
static const uint32 icon_document[32] = {
    0x00000000, 0x0FFC0000, 0x0FFE0000, 0x0C0F0000,
    0x0C070000, 0x0C030000, 0x0FFF0000, 0x0C030000,
    0x0C030000, 0x0FF30000, 0x0C030000, 0x0C030000,
    0x0FF30000, 0x0C030000, 0x0C030000, 0x0FF30000,
    0x0C030000, 0x0C030000, 0x0FF30000, 0x0C030000,
    0x0C030000, 0x0FF30000, 0x0C030000, 0x0C030000,
    0x0FFF0000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Palette icon (creative)
static const uint32 icon_palette[32] = {
    0x00000000, 0x00000000, 0x03FC0000, 0x0FFE0000,
    0x1C070000, 0x38C38000, 0x31C1C000, 0x63C0C000,
    0x63006000, 0xC3006000, 0xC6006000, 0xC600E000,
    0xC600E000, 0xC300C000, 0xC300C000, 0x61818000,
    0x61818000, 0x30C30000, 0x30C30000, 0x18660000,
    0x0C3C0000, 0x07F80000, 0x03F00000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Globe icon (network)
static const uint32 icon_globe[32] = {
    0x00000000, 0x00000000, 0x03FC0000, 0x0FFE0000,
    0x1FFF0000, 0x3E4F8000, 0x3C67C000, 0x7C63C000,
    0x7FE3E000, 0xFFC3E000, 0xFF03E000, 0xFFFFE000,
    0xFFFFE000, 0xFF03E000, 0xFFC3E000, 0x7FE3E000,
    0x7C63C000, 0x3C67C000, 0x3E4F8000, 0x1FFF0000,
    0x0FFE0000, 0x03FC0000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Gamepad icon (games)
static const uint32 icon_gamepad[32] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x1FF7F800, 0x3FFFFC00, 0x7FFFFE00,
    0x7CF8FE00, 0xFCF87F00, 0xFFF8FF00, 0xFCF87F00,
    0x7CF8FE00, 0x7FFFFE00, 0x3FFFFC00, 0x1FFFF800,
    0x0FFFF000, 0x07FFE000, 0x03FFC000, 0x01FF8000,
    0x00FF0000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Code brackets icon (developer)
static const uint32 icon_code[32] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x18060000, 0x30060000, 0x600C0000, 0xC00C0000,
    0xC0180000, 0x60180000, 0x60300000, 0x30300000,
    0x18600000, 0x18600000, 0x30300000, 0x60300000,
    0x60180000, 0xC0180000, 0xC00C0000, 0x600C0000,
    0x30060000, 0x18060000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Calculator icon (utility)
static const uint32 icon_calculator[32] = {
    0x00000000, 0x0FFE0000, 0x0FFE0000, 0x0C060000,
    0x0FFE0000, 0x0C060000, 0x0C060000, 0x0FFE0000,
    0x0C060000, 0x0CC60000, 0x0CC60000, 0x0C060000,
    0x0CC60000, 0x0CC60000, 0x0C060000, 0x0CC60000,
    0x0CC60000, 0x0C060000, 0x0FFE0000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// Folder icon
static const uint32 icon_folder[32] = {
    0x00000000, 0x00000000, 0x00000000, 0x3F000000,
    0x7F800000, 0xFFFE0000, 0xFFFF0000, 0xC0030000,
    0xC0030000, 0xC0030000, 0xC0030000, 0xC0030000,
    0xC0030000, 0xC0030000, 0xC0030000, 0xC0030000,
    0xC0030000, 0xFFFF0000, 0x7FFE0000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// File icon
static const uint32 icon_file[32] = {
    0x00000000, 0x0FF00000, 0x0FF80000, 0x0C1C0000,
    0x0C0E0000, 0x0C070000, 0x0C030000, 0x0C030000,
    0x0C030000, 0x0C030000, 0x0C030000, 0x0C030000,
    0x0C030000, 0x0C030000, 0x0C030000, 0x0C030000,
    0x0C030000, 0x0FFF0000, 0x0FFF0000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static const uint32* icon_bitmaps[] = {
    icon_gear,       // ICON_SYSTEM = 0
    icon_document,   // ICON_PRODUCTIVITY = 1
    icon_palette,    // ICON_CREATIVE = 2
    icon_globe,      // ICON_NETWORK = 3
    icon_gamepad,    // ICON_GAMES = 4
    icon_code,       // ICON_DEVELOPER = 5
    icon_calculator, // ICON_UTILITY = 6
    icon_folder,     // ICON_FOLDER = 7
    icon_file        // ICON_FILE = 8
};

// ============================================================
// Global state
// ============================================================
static neo::gui::Framebuffer g_fb;
static bool g_initialized = false;
neo::gui::Rect g_clip;
static bool g_clip_active = false;

// AGA bitplane pointers (chip RAM)
static uint8* g_aga_planes[8] = {};
static uint32 g_aga_plane_size = 0;
static uint8* g_aga_copper_list = nullptr;

// AGA palette remap table for alpha blending simulation
static uint8 g_aga_blend_table[INODE_SIZE];

// Cursor state
static int g_cursor_style = 0;

// ============================================================
// Inline clipping helpers
// ============================================================
static inline bool clip_point(int32 x, int32 y) {
    if (!g_clip_active) {
        return (x >= 0 && y >= 0 &&
                (uint32)x < g_fb.width && (uint32)y < g_fb.height);
    }
    return (x >= g_clip.x && y >= g_clip.y &&
            x < g_clip.x + (int32)g_clip.w && y < g_clip.y + (int32)g_clip.h);
}

static inline neo::gui::Rect clip_rect(const neo::gui::Rect& r) {
    int32 cx = 0, cy = 0;
    int32 cw = (int32)g_fb.width, ch = (int32)g_fb.height;
    if (g_clip_active) {
        cx = g_clip.x; cy = g_clip.y;
        cw = g_clip.x + (int32)g_clip.w;
        ch = g_clip.y + (int32)g_clip.h;
    }
    int32 x0 = max_i32(r.x, cx);
    int32 y0 = max_i32(r.y, cy);
    int32 x1 = min_i32(r.x + (int32)r.w, cw);
    int32 y1 = min_i32(r.y + (int32)r.h, ch);
    if (x1 <= x0 || y1 <= y0) return {0, 0, 0, 0};
    return {x0, y0, (uint32)(x1 - x0), (uint32)(y1 - y0)};
}

// ============================================================
// AGA chunky-to-planar conversion
// ============================================================
static void c2p_convert(uint8* chunky, uint32 width, uint32 height, uint32 pitch) {
    // Convert 8-bit chunky buffer to 8 interleaved bitplanes
    uint32 plane_pitch = (width + 15) / 16 * 2; // Word-aligned
    for (uint32 y = 0; y < height; y++) {
        uint8* src_row = chunky + y * pitch;
        for (uint32 x = 0; x < width; x += 8) {
            uint8 planes[8] = {0,0,0,0,0,0,0,0};
            uint32 remaining = min_u32(8, width - x);
            for (uint32 b = 0; b < remaining; b++) {
                uint8 pixel = src_row[x + b];
                for (int p = 0; p < 8; p++) {
                    if (pixel & (1 << p)) {
                        planes[p] |= (0x80 >> b);
                    }
                }
            }
            uint32 byte_offset = y * plane_pitch + (x >> 3);
            for (int p = 0; p < 8; p++) {
                g_aga_planes[p][byte_offset] = planes[p];
            }
        }
    }
}

// ============================================================
// AGA copper list setup for INODE_SIZE-color mode
// ============================================================
static void setup_aga_copper(uint32 width, uint32 height) {
    // Copper list: set bitplane pointers, enable 8-bitplane display
    // BPLCONx registers at 0xDFF100, 0xDFF104, 0xDFF108
    volatile uint16* custom = (volatile uint16*)0xDFF000;

    // BPLCON0: 8 bitplanes, COLOR mode
    custom[0x100/2] = 0x8200 | (8 << 12); // 8 planes + COLOR bit
    // BPLCON1: no scroll
    custom[0x102/2] = 0x0000;
    // BPLCON2: default priority
    custom[0x104/2] = 0x0024;

    uint32 plane_pitch = (width + 15) / 16 * 2;

    // Set bitplane pointers BPL1PTH/L through BPL8PTH/L
    for (int p = 0; p < 8; p++) {
        uint32 addr = (uint32)g_aga_planes[p];
        custom[(0x0E0 + p * 4) / 2] = (uint16)(addr >> 16);     // BPLxPTH
        custom[(0x0E2 + p * 4) / 2] = (uint16)(addr & 0xFFFF);  // BPLxPTL
    }

    // BPL1MOD, BPL2MOD = 0 (no interleave offset needed for separate planes)
    custom[0x108/2] = 0;
    custom[0x10A/2] = 0;

    // DIWSTRT / DIWSTOP for 640x480
    custom[0x08E/2] = 0x2C81; // DIWSTRT
    custom[0x090/2] = 0xF4C1; // DIWSTOP
    // DDFSTRT / DDFSTOP
    custom[0x092/2] = 0x003C;
    custom[0x094/2] = 0x00D4;
}

// ============================================================
// AGA palette setup - generate a default INODE_SIZE-color palette
// ============================================================
neo::gui::Color g_aga_palette[INODE_SIZE];

static void setup_aga_palette() {
    // Build a INODE_SIZE-color palette: 6x6x6 color cube (216) + 40 grays
    int idx = 0;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                g_aga_palette[idx].r = (uint8)(r * 51);
                g_aga_palette[idx].g = (uint8)(g * 51);
                g_aga_palette[idx].b = (uint8)(b * 51);
                g_aga_palette[idx].a = 255;
                idx++;
            }
        }
    }
    // 40 shades of gray
    for (int i = 0; i < 40; i++) {
        uint8 v = (uint8)(i * 255 / 39);
        g_aga_palette[idx].r = v;
        g_aga_palette[idx].g = v;
        g_aga_palette[idx].b = v;
        g_aga_palette[idx].a = 255;
        idx++;
    }

    // Write palette to AGA color registers
    // AGA uses BPLCON3 bank select + COLOR00-COLOR31 for INODE_SIZE colors
    volatile uint16* custom = (volatile uint16*)0xDFF000;
    for (int bank = 0; bank < 8; bank++) {
        // Select color bank via BPLCON3 (0xDFF106)
        custom[0x106/2] = (uint16)(bank << 13);
        for (int c = 0; c < 32; c++) {
            int ci = bank * 32 + c;
            // High nibbles in primary register
            uint16 hi = ((uint16)(g_aga_palette[ci].r & 0xF0) << 4) |
                        ((uint16)(g_aga_palette[ci].g & 0xF0)) |
                        ((uint16)(g_aga_palette[ci].b & 0xF0) >> 4);
            custom[(0x180 + c * 2) / 2] = hi;
        }
        // Low nibbles via LOCT bit in BPLCON3
        custom[0x106/2] = (uint16)((bank << 13) | 0x0200);
        for (int c = 0; c < 32; c++) {
            int ci = bank * 32 + c;
            uint16 lo = ((uint16)(g_aga_palette[ci].r & 0x0F) << 8) |
                        ((uint16)(g_aga_palette[ci].g & 0x0F) << 4) |
                        ((uint16)(g_aga_palette[ci].b & 0x0F));
            custom[(0x180 + c * 2) / 2] = lo;
        }
    }

    // Build blend lookup table - for each palette index, find the closest
    // tinted version (shifted toward blue/white for glass effect)
    for (int i = 0; i < INODE_SIZE; i++) {
        int32 tr = ((int32)g_aga_palette[i].r * 180 + 100 * 75) >> 8;
        int32 tg = ((int32)g_aga_palette[i].g * 180 + 130 * 75) >> 8;
        int32 tb = ((int32)g_aga_palette[i].b * 180 + 200 * 75) >> 8;
        tr = clamp_i32(tr, 0, 255);
        tg = clamp_i32(tg, 0, 255);
        tb = clamp_i32(tb, 0, 255);
        // Find nearest palette entry
        int best = 0;
        int32 best_dist = 0x7FFFFFFF;
        for (int j = 0; j < INODE_SIZE; j++) {
            int32 dr = tr - (int32)g_aga_palette[j].r;
            int32 dg = tg - (int32)g_aga_palette[j].g;
            int32 db = tb - (int32)g_aga_palette[j].b;
            int32 dist = dr * dr + dg * dg + db * db;
            if (dist < best_dist) {
                best_dist = dist;
                best = j;
            }
        }
        g_aga_blend_table[i] = (uint8)best;
    }
}

// Find nearest palette index for a given color
static uint8 find_aga_color(const neo::gui::Color& c) {
    // Quick lookup in 6x6x6 cube
    int ri = ((int32)c.r + 25) / 51;
    int gi = ((int32)c.g + 25) / 51;
    int bi = ((int32)c.b + 25) / 51;
    ri = clamp_i32(ri, 0, 5);
    gi = clamp_i32(gi, 0, 5);
    bi = clamp_i32(bi, 0, 5);
    return (uint8)(ri * 36 + gi * 6 + bi);
}

} // anonymous namespace

// ============================================================
// neo::gui implementation
// ============================================================
namespace neo {
namespace gui {

bool init(GfxMode mode) {
    if (g_initialized) return true;

    g_fb.mode = mode;
    g_fb.bpp = (mode == GFX_RTG) ? 16 : 8;
    g_clip_active = false;

    switch (mode) {
    case GFX_RTG:
        g_fb.width = 800;
        g_fb.height = 600;
        g_fb.pitch = g_fb.width * 2; // 16-bit RGB565
        g_fb.buffer = (uint8*)neo::memory::alloc(g_fb.pitch * g_fb.height, neo::memory::NB_MEMF_FAST);
        if (!g_fb.buffer) return false;
        // Clear to black
        for (uint32 i = 0; i < g_fb.pitch * g_fb.height; i++) {
            g_fb.buffer[i] = 0;
        }
        break;

    case GFX_AGA:
        g_fb.width = 640;
        g_fb.height = 480;
        g_fb.pitch = g_fb.width; // 8-bit chunky
        g_fb.buffer = (uint8*)neo::memory::alloc(g_fb.pitch * g_fb.height, neo::memory::NB_MEMF_FAST);
        if (!g_fb.buffer) return false;
        // Allocate bitplanes in chip RAM
        g_aga_plane_size = ((g_fb.width + 15) / 16 * 2) * g_fb.height;
        for (int p = 0; p < 8; p++) {
            g_aga_planes[p] = (uint8*)neo::memory::alloc(g_aga_plane_size, neo::memory::NB_MEMF_CHIP);
            if (!g_aga_planes[p]) return false;
        }
        setup_aga_palette();
        setup_aga_copper(g_fb.width, g_fb.height);
        // Clear chunky buffer
        for (uint32 i = 0; i < g_fb.pitch * g_fb.height; i++) {
            g_fb.buffer[i] = 0;
        }
        break;

    case GFX_ECS:
        g_fb.width = 640;
        g_fb.height = 480;
        g_fb.pitch = g_fb.width; // 8-bit chunky (only 16 colors used)
        g_fb.buffer = (uint8*)neo::memory::alloc(g_fb.pitch * g_fb.height, neo::memory::NB_MEMF_FAST);
        if (!g_fb.buffer) return false;
        // Allocate 4 bitplanes for 16 colors
        g_aga_plane_size = ((g_fb.width + 15) / 16 * 2) * g_fb.height;
        for (int p = 0; p < 4; p++) {
            g_aga_planes[p] = (uint8*)neo::memory::alloc(g_aga_plane_size, neo::memory::NB_MEMF_CHIP);
            if (!g_aga_planes[p]) return false;
        }
        for (int p = 4; p < 8; p++) {
            g_aga_planes[p] = nullptr;
        }
        // Set up basic 16-color OCS/ECS palette
        {
            volatile uint16* custom = (volatile uint16*)0xDFF000;
            static const uint16 ecs_pal[16] = {
                0x000, 0xFFF, 0xF00, 0x0F0, 0x00F, 0xFF0, 0x0FF, 0xF0F,
                0x888, 0xCCC, 0x800, 0x080, 0x008, 0x880, 0x088, 0x808
            };
            for (int i = 0; i < 16; i++) {
                g_aga_palette[i] = {
                    (uint8)((ecs_pal[i] >> 8) * 17),
                    (uint8)(((ecs_pal[i] >> 4) & 0xF) * 17),
                    (uint8)((ecs_pal[i] & 0xF) * 17),
                    255
                };
                custom[(0x180 + i * 2) / 2] = ecs_pal[i];
            }
        }
        for (uint32 i = 0; i < g_fb.pitch * g_fb.height; i++) {
            g_fb.buffer[i] = 0;
        }
        break;
    }

    g_initialized = true;
    return true;
}

Framebuffer* get_framebuffer() {
    return &g_fb;
}

GfxMode get_mode() {
    return g_fb.mode;
}

void shutdown() {
    if (!g_initialized) return;
    if (g_fb.buffer) {
        neo::memory::free(g_fb.buffer);
        g_fb.buffer = nullptr;
    }
    for (int p = 0; p < 8; p++) {
        if (g_aga_planes[p]) {
            neo::memory::free(g_aga_planes[p]);
            g_aga_planes[p] = nullptr;
        }
    }
    g_initialized = false;
}

void begin_frame() {
    // Nothing special needed - we draw to back buffer
}

void end_frame() {
    if (g_fb.mode == GFX_RTG) {
        // Copy back buffer to VRAM
        // RTG card VRAM is typically memory-mapped; for now copy to the same buffer
        // In a real setup, this would blit to the RTG card's display memory
        // The display driver handles the actual VRAM mapping
        // neo::display::blit_to_screen(g_fb.buffer, g_fb.width, g_fb.height, g_fb.pitch);
    } else if (g_fb.mode == GFX_AGA) {
        c2p_convert(g_fb.buffer, g_fb.width, g_fb.height, g_fb.pitch);
    } else {
        // ECS: c2p with only 4 planes
        uint32 plane_pitch = (g_fb.width + 15) / 16 * 2;
        for (uint32 y = 0; y < g_fb.height; y++) {
            uint8* src_row = g_fb.buffer + y * g_fb.pitch;
            for (uint32 x = 0; x < g_fb.width; x += 8) {
                uint8 planes[4] = {0,0,0,0};
                uint32 remaining = min_u32(8, g_fb.width - x);
                for (uint32 b = 0; b < remaining; b++) {
                    uint8 pixel = src_row[x + b] & 0x0F;
                    for (int p = 0; p < 4; p++) {
                        if (pixel & (1 << p)) {
                            planes[p] |= (0x80 >> b);
                        }
                    }
                }
                uint32 byte_offset = y * plane_pitch + (x >> 3);
                for (int p = 0; p < 4; p++) {
                    g_aga_planes[p][byte_offset] = planes[p];
                }
            }
        }
    }
}

void put_pixel(int32 x, int32 y, const Color& c) {
    if (!clip_point(x, y)) return;
    if (g_fb.bpp == 16) {
        uint16* row = (uint16*)(g_fb.buffer + y * g_fb.pitch);
        row[x] = neo::gui::to_rgb565(c);
    } else {
        g_fb.buffer[y * g_fb.pitch + x] = find_aga_color(c);
    }
}

void fill_rect(const Rect& r, const Color& c) {
    Rect cr = clip_rect(r);
    if (cr.w == 0 || cr.h == 0) return;

    if (g_fb.bpp == 16) {
        uint16 pixel = neo::gui::to_rgb565(c);
        for (uint32 y = 0; y < cr.h; y++) {
            uint16* row = (uint16*)(g_fb.buffer + (cr.y + y) * g_fb.pitch);
            for (uint32 x = 0; x < cr.w; x++) {
                row[cr.x + x] = pixel;
            }
        }
    } else {
        uint8 pixel = find_aga_color(c);
        for (uint32 y = 0; y < cr.h; y++) {
            uint8* row = g_fb.buffer + (cr.y + y) * g_fb.pitch;
            for (uint32 x = 0; x < cr.w; x++) {
                row[cr.x + x] = pixel;
            }
        }
    }
}

void draw_rect(const Rect& r, const Color& c) {
    // Top edge
    fill_rect({r.x, r.y, r.w, 1}, c);
    // Bottom edge
    fill_rect({r.x, r.y + (int32)r.h - 1, r.w, 1}, c);
    // Left edge
    fill_rect({r.x, r.y, 1, r.h}, c);
    // Right edge
    fill_rect({r.x + (int32)r.w - 1, r.y, 1, r.h}, c);
}

void draw_line(int32 x0, int32 y0, int32 x1, int32 y1, const Color& c) {
    // Bresenham's line algorithm
    int32 dx = x1 - x0;
    int32 dy = y1 - y0;
    int32 sx = (dx >= 0) ? 1 : -1;
    int32 sy = (dy >= 0) ? 1 : -1;
    dx = (dx >= 0) ? dx : -dx;
    dy = (dy >= 0) ? dy : -dy;

    if (dx >= dy) {
        int32 err = dx >> 1;
        int32 y = y0;
        for (int32 x = x0; x != x1 + sx; x += sx) {
            put_pixel(x, y, c);
            err -= dy;
            if (err < 0) {
                y += sy;
                err += dx;
            }
        }
    } else {
        int32 err = dy >> 1;
        int32 x = x0;
        for (int32 y = y0; y != y1 + sy; y += sy) {
            put_pixel(x, y, c);
            err -= dx;
            if (err < 0) {
                x += sx;
                err += dy;
            }
        }
    }
}

void blit(const uint8* src_buf, uint32 src_pitch, int32 dst_x, int32 dst_y, uint32 w, uint32 h) {
    for (uint32 y = 0; y < h; y++) {
        int32 dy = dst_y + (int32)y;
        if (dy < 0 || (uint32)dy >= g_fb.height) continue;
        const uint8* src_row = src_buf + y * src_pitch;
        uint8* dst_row = g_fb.buffer + dy * g_fb.pitch;
        for (uint32 x = 0; x < w; x++) {
            int32 dx = dst_x + (int32)x;
            if (dx < 0 || (uint32)dx >= g_fb.width) continue;
            if (g_clip_active && !clip_point(dx, dy)) continue;
            if (g_fb.bpp == 16) {
                ((uint16*)dst_row)[dx] = ((const uint16*)src_row)[x];
            } else {
                dst_row[dx] = src_row[x];
            }
        }
    }
}

void alpha_blend(const Rect& r, const Color& c) {
    Rect cr = clip_rect(r);
    if (cr.w == 0 || cr.h == 0) return;

    if (g_fb.bpp == 16) {
        uint32 alpha = c.a;
        uint32 inv_alpha = 255 - alpha;
        uint32 sr = c.r;
        uint32 sg = c.g;
        uint32 sb = c.b;
        for (uint32 y = 0; y < cr.h; y++) {
            uint16* row = (uint16*)(g_fb.buffer + (cr.y + y) * g_fb.pitch);
            for (uint32 x = 0; x < cr.w; x++) {
                uint16 dst = row[cr.x + x];
                uint32 dr = (dst >> 11) & 0x1F;
                uint32 dg = (dst >> 5) & 0x3F;
                uint32 db = dst & 0x1F;
                // Expand to 8-bit
                dr = (dr << 3) | (dr >> 2);
                dg = (dg << 2) | (dg >> 4);
                db = (db << 3) | (db >> 2);
                // Blend
                uint32 rr = (sr * alpha + dr * inv_alpha) >> 8;
                uint32 rg = (sg * alpha + dg * inv_alpha) >> 8;
                uint32 rb = (sb * alpha + db * inv_alpha) >> 8;
                // Pack back to RGB565
                row[cr.x + x] = ((rr >> 3) << 11) | ((rg >> 2) << 5) | (rb >> 3);
            }
        }
    } else {
        // AGA/ECS: use palette remap table for blending approximation
        for (uint32 y = 0; y < cr.h; y++) {
            uint8* row = g_fb.buffer + (cr.y + y) * g_fb.pitch;
            for (uint32 x = 0; x < cr.w; x++) {
                row[cr.x + x] = g_aga_blend_table[row[cr.x + x]];
            }
        }
    }
}

void draw_text(int32 x, int32 y, const char* text, const Color& c, FontSize font_size) {
    if (!text) return;
    const uint8* font;
    int glyph_h, glyph_w, glyph_bytes;
    if (font_size == FontSize::Small) {
        font = font_6x10;
        glyph_h = 10;
        glyph_w = 6;
        glyph_bytes = 10;
    } else {
        font = font_8x16;
        glyph_h = 16;
        glyph_w = 8;
        glyph_bytes = 16;
    }

    int32 cx = x;
    while (*text) {
        uint8 ch = (uint8)*text;
        if (ch >= 32 && ch < 127) {
            int glyph_idx = ch - 32;
            const uint8* glyph = font + glyph_idx * glyph_bytes;
            for (int row = 0; row < glyph_h; row++) {
                uint8 bits = glyph[row];
                for (int col = 0; col < glyph_w; col++) {
                    if (bits & (0x80 >> col)) {
                        put_pixel(cx + col, y + row, c);
                    }
                }
            }
        }
        if ((int)font_size >= 2 && ch >= 32 && ch < 127) {
            cx += font_prop_widths[ch - 32];
        } else {
            cx += glyph_w;
        }
        text++;
    }
}

uint32 text_width(const char* text, int font_size) {
    if (!text) return 0;
    uint32 w = 0;
    int glyph_w;
    if (font_size == 0) {
        glyph_w = 6;
    } else if (font_size == 1) {
        glyph_w = 8;
    } else {
        glyph_w = 0; // proportional
    }

    while (*text) {
        uint8 ch = (uint8)*text;
        if (font_size >= 2 && ch >= 32 && ch < 127) {
            w += font_prop_widths[ch - 32];
        } else {
            w += glyph_w;
        }
        text++;
    }
    return w;
}

uint32 text_height(int font_size) {
    if (font_size == 0) return 10;
    return 16;
}

void fill_rounded_rect(const Rect& r, uint32 radius, const Color& c) {
    // Same as draw_rounded_rect but fills interior (it already fills)
    draw_rounded_rect(r, radius, c);
}

void draw_rounded_rect(const Rect& r, uint32 radius, const Color& c) {
    if (r.w == 0 || r.h == 0) return;
    if (radius > r.w / 2) radius = r.w / 2;
    if (radius > r.h / 2) radius = r.h / 2;

    // Fill center
    fill_rect({r.x + (int32)radius, r.y, r.w - radius * 2, r.h}, c);
    // Fill left/right strips
    fill_rect({r.x, r.y + (int32)radius, radius, r.h - radius * 2}, c);
    fill_rect({r.x + (int32)r.w - (int32)radius, r.y + (int32)radius, radius, r.h - radius * 2}, c);

    // Draw corner quadrants using midpoint circle algorithm
    int32 cx, cy;
    int32 px = (int32)radius;
    int32 py = 0;
    int32 err = 1 - px;

    while (px >= py) {
        // Top-left corner: center at (r.x+radius, r.y+radius)
        cx = r.x + (int32)radius;
        cy = r.y + (int32)radius;
        fill_rect({cx - px, cy - py, (uint32)px, 1}, c);
        fill_rect({cx - py, cy - px, (uint32)py, 1}, c);

        // Top-right corner: center at (r.x+r.w-radius-1, r.y+radius)
        cx = r.x + (int32)r.w - (int32)radius - 1;
        cy = r.y + (int32)radius;
        fill_rect({cx + 1, cy - py, (uint32)px, 1}, c);
        fill_rect({cx + 1, cy - px, (uint32)py, 1}, c);

        // Bottom-left corner
        cx = r.x + (int32)radius;
        cy = r.y + (int32)r.h - (int32)radius - 1;
        fill_rect({cx - px, cy + py, (uint32)px, 1}, c);
        fill_rect({cx - py, cy + px, (uint32)py, 1}, c);

        // Bottom-right corner
        cx = r.x + (int32)r.w - (int32)radius - 1;
        cy = r.y + (int32)r.h - (int32)radius - 1;
        fill_rect({cx + 1, cy + py, (uint32)px, 1}, c);
        fill_rect({cx + 1, cy + px, (uint32)py, 1}, c);

        py++;
        if (err < 0) {
            err += 2 * py + 1;
        } else {
            px--;
            err += 2 * (py - px) + 1;
        }
    }
}

void draw_gradient_rect(const Rect& r, const Color& top, const Color& bottom) {
    Rect cr = clip_rect(r);
    if (cr.w == 0 || cr.h == 0) return;

    for (uint32 y = 0; y < cr.h; y++) {
        // Calculate interpolation factor relative to original rect
        uint32 oy = (cr.y + y) - r.y;
        uint32 h = r.h > 0 ? r.h : 1;
        Color row_color;
        row_color.r = (uint8)(((uint32)top.r * (h - 1 - oy) + (uint32)bottom.r * oy) / (h - 1 > 0 ? h - 1 : 1));
        row_color.g = (uint8)(((uint32)top.g * (h - 1 - oy) + (uint32)bottom.g * oy) / (h - 1 > 0 ? h - 1 : 1));
        row_color.b = (uint8)(((uint32)top.b * (h - 1 - oy) + (uint32)bottom.b * oy) / (h - 1 > 0 ? h - 1 : 1));
        row_color.a = 255;

        if (g_fb.bpp == 16) {
            uint16 pixel = neo::gui::to_rgb565(row_color);
            uint16* row = (uint16*)(g_fb.buffer + (cr.y + y) * g_fb.pitch);
            for (uint32 x = 0; x < cr.w; x++) {
                row[cr.x + x] = pixel;
            }
        } else {
            uint8 pixel = find_aga_color(row_color);
            uint8* row = g_fb.buffer + (cr.y + y) * g_fb.pitch;
            for (uint32 x = 0; x < cr.w; x++) {
                row[cr.x + x] = pixel;
            }
        }
    }
}

void set_clip(const Rect& r) {
    g_clip = r;
    g_clip_active = true;
}

void clear_clip() {
    g_clip_active = false;
}

uint32 screen_width() {
    return g_fb.width;
}

uint32 screen_height() {
    return g_fb.height;
}

// ============================================================
// Cursor namespace
// ============================================================
namespace cursor {

void draw(int32 x, int32 y) {
    const uint16* shape;
    const uint16* mask;
    switch (g_cursor_style) {
    case 1:  shape = cursor_hand;   mask = cursor_hand_mask;   break;
    case 2:  shape = cursor_resize; mask = cursor_resize_mask; break;
    default: shape = cursor_arrow;  mask = cursor_arrow_mask;  break;
    }

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int32 px = x + col;
            int32 py = y + row;
            uint16 bit = 0x8000 >> col;
            if (mask[row] & bit) {
                Color c;
                if (shape[row] & bit) {
                    c = {255, 255, 255, 255}; // White fill
                } else {
                    c = {0, 0, 0, 255};       // Black outline
                }
                put_pixel(px, py, c);
            }
        }
    }
}

void set_style(int style) {
    g_cursor_style = style;
}

} // namespace cursor

// ============================================================
// Wallpaper namespace
// ============================================================
namespace wallpaper {

static void gen_aurora(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    rng_state = 42;
    for (uint32 y = 0; y < h; y++) {
        for (uint32 x = 0; x < w; x++) {
            // Dark blue/black sky gradient
            int32 sky_r = 0;
            int32 sky_g = 0;
            int32 sky_b = (int32)(y * 30 / h);

            // Aurora bands - sine waves across the screen
            int32 aurora_r = 0, aurora_g = 0, aurora_b = 0;
            for (int band = 0; band < 3; band++) {
                int32 freq = 2 + band;
                int32 phase = band * 85;
                int32 yoff = (int32)(h / 3) + band * (int32)(h / 8);
                int32 wave = fp_sin((uint32)(x * freq * INODE_SIZE / (int32)w + phase)) >> 9; // [-64..64]
                int32 band_y = yoff + wave;
                int32 dist = (int32)y - band_y;
                if (dist < 0) dist = -dist;
                int32 intensity = 80 - dist;
                if (intensity < 0) intensity = 0;
                if (intensity > 80) intensity = 80;
                intensity = intensity * intensity / 80;
                if (band == 0) { aurora_g += intensity; aurora_b += intensity / 3; }
                else if (band == 1) { aurora_g += intensity / 2; aurora_b += intensity; }
                else { aurora_r += intensity / 2; aurora_g += intensity; }
            }

            int32 r = clamp_i32(sky_r + aurora_r, 0, 255);
            int32 g = clamp_i32(sky_g + aurora_g, 0, 255);
            int32 b = clamp_i32(sky_b + aurora_b, 0, 255);

            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                buf[y * w + x] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }
    // Star field
    rng_state = 777;
    for (int s = 0; s < 200; s++) {
        uint32 sx = rng_next() % w;
        uint32 sy = rng_next() % (h * 2 / 3);
        uint8 brightness = (uint8)(128 + (rng_next() % 128));
        if (bpp == 16) {
            uint16* p = (uint16*)(buf + sy * w * 2);
            p[sx] = ((brightness >> 3) << 11) | ((brightness >> 2) << 5) | (brightness >> 3);
        } else {
            buf[sy * w + sx] = find_aga_color({brightness, brightness, brightness, 255});
        }
    }
}

static void gen_circuit(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    // Dark green/black background
    for (uint32 y = 0; y < h; y++) {
        for (uint32 x = 0; x < w; x++) {
            int32 r = 0, g = (int32)(5 + y * 10 / h), b = 0;
            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                buf[y * w + x] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }

    // Circuit traces
    rng_state = 31337;
    for (int trace = 0; trace < 40; trace++) {
        uint32 tx = rng_next() % w;
        uint32 ty = rng_next() % h;
        int dir = rng_next() % 2; // 0=horizontal, 1=vertical
        uint32 len = 40 + rng_next() % 120;
        uint8 bright = (uint8)(40 + rng_next() % 60);
        Color tc = {0, bright, 0, 255};
        for (uint32 i = 0; i < len; i++) {
            uint32 px, py;
            if (dir == 0) { px = tx + i; py = ty; }
            else { px = tx; py = ty + i; }
            if (px >= w || py >= h) break;
            if (bpp == 16) {
                uint16* p = (uint16*)(buf + py * w * 2);
                p[px] = ((tc.r >> 3) << 11) | ((tc.g >> 2) << 5) | (tc.b >> 3);
            } else {
                buf[py * w + px] = find_aga_color(tc);
            }
        }
        // Node (bright dot at intersection)
        uint32 nx = tx, ny = ty;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                uint32 px = nx + dx, py = ny + dy;
                if (px < w && py < h) {
                    Color nc = {0, 200, 0, 255};
                    if (bpp == 16) {
                        uint16* p = (uint16*)(buf + py * w * 2);
                        p[px] = ((nc.r >> 3) << 11) | ((nc.g >> 2) << 5) | (nc.b >> 3);
                    } else {
                        buf[py * w + px] = find_aga_color(nc);
                    }
                }
            }
        }
    }
}

static void gen_retrowave(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    uint32 horizon = h * 55 / 100;

    for (uint32 y = 0; y < h; y++) {
        for (uint32 x = 0; x < w; x++) {
            int32 r, g, b;

            if (y < horizon) {
                // Sky gradient: deep purple → hot pink → orange
                int32 t = (int32)y * INODE_SIZE / (int32)horizon;
                if (t < 128) {
                    r = 30 + t * 150 / 128;
                    g = 0 + t * 20 / 128;
                    b = 80 + t * 80 / 128;
                } else {
                    int32 t2 = t - 128;
                    r = 180 + t2 * 75 / 128;
                    g = 20 + t2 * 80 / 128;
                    b = 160 - t2 * 120 / 128;
                }

                // Sun circle
                int32 sun_cx = (int32)w / 2;
                int32 sun_cy = (int32)horizon - (int32)(h / 6);
                int32 sun_r = (int32)(h / 8);
                int32 dx = (int32)x - sun_cx;
                int32 dy = (int32)y - sun_cy;
                int32 dist_sq = dx * dx + dy * dy;
                int32 rad_sq = sun_r * sun_r;
                if (dist_sq < rad_sq) {
                    // Inside sun - yellow/orange with horizontal stripe cutouts
                    int32 sun_row = ((int32)y - sun_cy + sun_r);
                    if ((sun_row / 4) % 2 == 0) {
                        r = 255; g = 200; b = 50;
                    }
                    // Else keep sky color (creates horizontal line effect)
                }
            } else {
                // Ground: perspective grid
                int32 gy = (int32)y - (int32)horizon;
                int32 depth = gy > 0 ? gy : 1;

                // Base ground color: dark purple
                r = 20 + gy * 10 / (int32)(h - horizon);
                g = 0;
                b = 40 + gy * 20 / (int32)(h - horizon);

                // Horizontal grid lines
                int32 grid_spacing = 2048 / (depth + 1);
                if (grid_spacing > 0 && (gy % (grid_spacing > 0 ? max_i32(grid_spacing, 1) : 1)) < 2) {
                    r = 200; g = 0; b = 200;
                }

                // Vertical grid lines (perspective)
                int32 center = (int32)w / 2;
                int32 offset = (int32)x - center;
                int32 grid_x = (offset * 16) / (depth + 1);
                if (grid_x < 0) grid_x = -grid_x;
                if ((grid_x % 40) < 2) {
                    r = 200; g = 0; b = 200;
                }
            }

            r = clamp_i32(r, 0, 255);
            g = clamp_i32(g, 0, 255);
            b = clamp_i32(b, 0, 255);

            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                buf[y * w + x] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }
}

static void gen_nebula(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    for (uint32 y = 0; y < h; y++) {
        for (uint32 x = 0; x < w; x++) {
            // Dark background
            int32 r = 2, g = 2, b = 8;

            // Three colored gas clouds using distance fields
            struct { int32 cx, cy, radius; int32 cr, cg, cb; } clouds[3] = {
                {(int32)w * 30 / 100, (int32)h * 40 / 100, (int32)w / 3, 80, 20, 60},
                {(int32)w * 70 / 100, (int32)h * 30 / 100, (int32)w / 4, 20, 40, 80},
                {(int32)w * 50 / 100, (int32)h * 70 / 100, (int32)w / 3, 60, 10, 40}
            };

            for (int ci = 0; ci < 3; ci++) {
                int32 dx = (int32)x - clouds[ci].cx;
                int32 dy = (int32)y - clouds[ci].cy;
                uint32 dist = isqrt((uint32)(dx * dx + dy * dy));
                int32 rad = clouds[ci].radius;
                if ((int32)dist < rad) {
                    int32 intensity = (rad - (int32)dist) * 100 / rad;
                    // Add some noise using sine
                    int32 noise = fp_sin((uint32)(x * 7 + y * 13)) >> 12;
                    intensity += noise;
                    intensity = clamp_i32(intensity, 0, 100);
                    r += clouds[ci].cr * intensity / 100;
                    g += clouds[ci].cg * intensity / 100;
                    b += clouds[ci].cb * intensity / 100;
                }
            }

            r = clamp_i32(r, 0, 255);
            g = clamp_i32(g, 0, 255);
            b = clamp_i32(b, 0, 255);

            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                buf[y * w + x] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }
    // Bright stars
    rng_state = 999;
    for (int s = 0; s < 300; s++) {
        uint32 sx = rng_next() % w;
        uint32 sy = rng_next() % h;
        uint8 brightness = (uint8)(180 + rng_next() % 76);
        Color sc = {brightness, brightness, brightness, 255};
        if (bpp == 16) {
            uint16* p = (uint16*)(buf + sy * w * 2);
            p[sx] = ((sc.r >> 3) << 11) | ((sc.g >> 2) << 5) | (sc.b >> 3);
        } else {
            buf[sy * w + sx] = find_aga_color(sc);
        }
    }
}

static void gen_matrix(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    // Black background
    for (uint32 y = 0; y < h; y++) {
        for (uint32 x = 0; x < w; x++) {
            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = 0;
            } else {
                buf[y * w + x] = 0;
            }
        }
    }

    // Falling character columns
    rng_state = 1234;
    int col_spacing = 12;
    int num_cols = (int)w / col_spacing;

    for (int col = 0; col < num_cols; col++) {
        uint32 cx = (uint32)(col * col_spacing + 2);
        int32 col_height = 8 + (int32)(rng_next() % 20);
        int32 col_start = (int32)(rng_next() % h);

        for (int32 i = 0; i < col_height; i++) {
            int32 cy = col_start + i * 14;
            if (cy < 0 || (uint32)cy + 14 > h) continue;

            // Brightness fades toward the tail
            int32 brightness;
            if (i == 0) {
                brightness = 255; // Head is brightest white-green
            } else {
                brightness = 200 - i * (160 / col_height);
                if (brightness < 30) brightness = 30;
            }

            // Draw a random character glyph from font
            uint8 ch = 33 + (uint8)(rng_next() % 93); // Random ASCII printable
            int glyph_idx = ch - 32;
            if (glyph_idx >= 0 && glyph_idx < 95) {
                const uint8* glyph = font_8x16 + glyph_idx * 16;
                for (int row = 0; row < 16; row++) {
                    uint8 bits = glyph[row];
                    for (int bit = 0; bit < 8; bit++) {
                        if (bits & (0x80 >> bit)) {
                            uint32 px = cx + bit;
                            uint32 py = (uint32)cy + row;
                            if (px < w && py < h) {
                                int32 r_val = (i == 0) ? brightness / 2 : 0;
                                int32 g_val = brightness;
                                int32 b_val = (i == 0) ? brightness / 3 : 0;
                                Color mc = {(uint8)r_val, (uint8)g_val, (uint8)b_val, 255};
                                if (bpp == 16) {
                                    uint16* p = (uint16*)(buf + py * w * 2);
                                    p[px] = ((mc.r >> 3) << 11) | ((mc.g >> 2) << 5) | (mc.b >> 3);
                                } else {
                                    buf[py * w + px] = find_aga_color(mc);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // NeoBench watermark in center
    const char* watermark = "NeoBench";
    int32 wx = (int32)(w / 2 - 32);
    int32 wy = (int32)(h / 2 - 8);
    for (int ci = 0; watermark[ci]; ci++) {
        uint8 ch = (uint8)watermark[ci];
        if (ch >= 32 && ch < 127) {
            const uint8* glyph = font_8x16 + (ch - 32) * 16;
            for (int row = 0; row < 16; row++) {
                uint8 bits = glyph[row];
                for (int bit = 0; bit < 8; bit++) {
                    if (bits & (0x80 >> bit)) {
                        uint32 px = (uint32)(wx + ci * 8 + bit);
                        uint32 py = (uint32)(wy + row);
                        if (px < w && py < h) {
                            Color wc = {0, 60, 0, 255};
                            if (bpp == 16) {
                                uint16* p = (uint16*)(buf + py * w * 2);
                                p[px] = ((wc.r >> 3) << 11) | ((wc.g >> 2) << 5) | (wc.b >> 3);
                            } else {
                                buf[py * w + px] = find_aga_color(wc);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void gen_neologo(uint8* buf, uint32 w, uint32 h, uint32 bpp) {
    // Dark gradient background
    for (uint32 y = 0; y < h; y++) {
        int32 r = (int32)(y * 15 / h);
        int32 g = (int32)(y * 10 / h);
        int32 b = (int32)(20 + y * 30 / h);
        for (uint32 x = 0; x < w; x++) {
            if (bpp == 16) {
                uint16* p = (uint16*)(buf + y * w * 2);
                p[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                buf[y * w + x] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }

    // Radial glow lines from center
    int32 center_x = (int32)w / 2;
    int32 center_y = (int32)h / 2;
    for (int angle = 0; angle < INODE_SIZE; angle += 4) {
        int32 dx = fp_cos((uint32)angle);
        int32 dy = fp_sin((uint32)angle);
        for (int32 dist = 60; dist < (int32)(w / 3); dist += 2) {
            int32 px = center_x + (dx * dist >> 15);
            int32 py = center_y + (dy * dist >> 15);
            if (px < 0 || py < 0 || (uint32)px >= w || (uint32)py >= h) continue;
            int32 intensity = 40 - dist * 40 / (int32)(w / 3);
            if (intensity < 0) intensity = 0;
            // Add blue-white glow
            int32 r = intensity / 2;
            int32 g = intensity / 2;
            int32 b = intensity;
            // Read existing and add
            if (bpp == 16) {
                uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                uint16 existing = p[(uint32)px];
                int32 er = ((existing >> 11) & 0x1F) << 3;
                int32 eg = ((existing >> 5) & 0x3F) << 2;
                int32 eb = (existing & 0x1F) << 3;
                r = clamp_i32(er + r, 0, 255);
                g = clamp_i32(eg + g, 0, 255);
                b = clamp_i32(eb + b, 0, 255);
                p[(uint32)px] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                uint8 ei = buf[(uint32)py * w + (uint32)px];
                int32 er = g_aga_palette[ei].r;
                int32 eg = g_aga_palette[ei].g;
                int32 eb = g_aga_palette[ei].b;
                r = clamp_i32(er + r, 0, 255);
                g = clamp_i32(eg + g, 0, 255);
                b = clamp_i32(eb + b, 0, 255);
                buf[(uint32)py * w + (uint32)px] = find_aga_color({(uint8)r, (uint8)g, (uint8)b, 255});
            }
        }
    }

    // Draw "NB" logo in the center using large block letters
    // N: two vertical bars + diagonal
    int32 logo_x = center_x - 40;
    int32 logo_y = center_y - 30;
    int32 logo_size = 60;
    Color logo_color = {180, 200, 255, 255};

    // Letter N
    for (int32 dy = 0; dy < logo_size; dy++) {
        for (int32 dx = 0; dx < 10; dx++) {
            // Left vertical bar
            int32 px = logo_x + dx;
            int32 py = logo_y + dy;
            if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                if (bpp == 16) {
                    uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                    p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                } else {
                    buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                }
            }
            // Right vertical bar
            px = logo_x + 30 + dx;
            if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                if (bpp == 16) {
                    uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                    p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                } else {
                    buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                }
            }
        }
        // Diagonal
        int32 diag_x = logo_x + 10 + dy * 20 / logo_size;
        for (int32 dx = 0; dx < 8; dx++) {
            int32 px = diag_x + dx;
            int32 py = logo_y + dy;
            if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                if (bpp == 16) {
                    uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                    p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                } else {
                    buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                }
            }
        }
    }

    // Letter B
    int32 bx = logo_x + 50;
    for (int32 dy = 0; dy < logo_size; dy++) {
        // Left vertical bar
        for (int32 dx = 0; dx < 10; dx++) {
            int32 px = bx + dx;
            int32 py = logo_y + dy;
            if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                if (bpp == 16) {
                    uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                    p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                } else {
                    buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                }
            }
        }
    }
    // Top, middle, bottom horizontal bars of B
    for (int bar = 0; bar < 3; bar++) {
        int32 bar_y;
        if (bar == 0) bar_y = logo_y;
        else if (bar == 1) bar_y = logo_y + logo_size / 2 - 4;
        else bar_y = logo_y + logo_size - 8;
        for (int32 dy = 0; dy < 8; dy++) {
            for (int32 dx = 10; dx < 30; dx++) {
                int32 px = bx + dx;
                int32 py = bar_y + dy;
                if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                    if (bpp == 16) {
                        uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                        p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                    } else {
                        buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                    }
                }
            }
        }
    }
    // Right curves of B (simplified as vertical bars)
    for (int half = 0; half < 2; half++) {
        int32 start_y = logo_y + half * (logo_size / 2);
        int32 end_y = start_y + logo_size / 2;
        for (int32 dy = start_y + 8; dy < end_y - 4; dy++) {
            for (int32 dx = 0; dx < 8; dx++) {
                int32 px = bx + 28 + dx;
                int32 py = dy;
                if (px >= 0 && py >= 0 && (uint32)px < w && (uint32)py < h) {
                    if (bpp == 16) {
                        uint16* p = (uint16*)(buf + (uint32)py * w * 2);
                        p[(uint32)px] = neo::gui::to_rgb565(logo_color);
                    } else {
                        buf[(uint32)py * w + (uint32)px] = find_aga_color(logo_color);
                    }
                }
            }
        }
    }
}

void generate(Type type, uint8* buffer, uint32 w, uint32 h, uint32 bpp) {
    switch (type) {
    case AURORA:    gen_aurora(buffer, w, h, bpp); break;
    case CIRCUIT:   gen_circuit(buffer, w, h, bpp); break;
    case RETROWAVE: gen_retrowave(buffer, w, h, bpp); break;
    case NEBULA:    gen_nebula(buffer, w, h, bpp); break;
    case MATRIX:    gen_matrix(buffer, w, h, bpp); break;
    case NEOLOGO:   gen_neologo(buffer, w, h, bpp); break;
    default:        gen_aurora(buffer, w, h, bpp); break;
    }
}

const char* get_name(Type type) {
    switch (type) {
    case AURORA:    return "Aurora";
    case CIRCUIT:   return "Circuit";
    case RETROWAVE: return "Retrowave";
    case NEBULA:    return "Nebula";
    case MATRIX:    return "Matrix";
    case NEOLOGO:   return "NeoBench";
    default:        return "Unknown";
    }
}

} // namespace wallpaper

// ============================================================
// Icon namespace
// ============================================================
namespace icon {

void draw_icon(int32 x, int32 y, IconId id) {
    int icon_id = (int)id;
    if (icon_id < 0 || icon_id > 8) icon_id = 8; // Default to file icon
    const uint32* bitmap = icon_bitmaps[icon_id];

    Color fg = {220, 220, 240, 255};
    Color shadow = {0, 0, 0, 128};

    // Draw shadow offset (+1, +1)
    for (int row = 0; row < 32; row++) {
        uint32 bits = bitmap[row];
        for (int col = 0; col < 32; col++) {
            if (bits & (0x80000000u >> col)) {
                put_pixel(x + col + 1, y + row + 1, shadow);
            }
        }
    }

    // Draw icon
    for (int row = 0; row < 32; row++) {
        uint32 bits = bitmap[row];
        for (int col = 0; col < 32; col++) {
            if (bits & (0x80000000u >> col)) {
                put_pixel(x + col, y + row, fg);
            }
        }
    }
}

void draw_icon_color(int32 x, int32 y, IconId id, const Color& c) {
    int icon_id = (int)id;
    if (icon_id < 0 || icon_id > 8) icon_id = 8;
    const uint32* bitmap = icon_bitmaps[icon_id];

    for (int row = 0; row < 32; row++) {
        uint32 bits = bitmap[row];
        for (int col = 0; col < 32; col++) {
            if (bits & (0x80000000u >> col)) {
                put_pixel(x + col, y + row, c);
            }
        }
    }
}

} // namespace icon

// ============================================================
// Palette namespace
// ============================================================
namespace palette {

void set_entry(uint32 index, const Color& c) {
    if (index >= INODE_SIZE) return;
    g_aga_palette[index] = c;

    if (g_fb.mode == GFX_AGA || g_fb.mode == GFX_ECS) {
        volatile uint16* custom = (volatile uint16*)0xDFF000;
        uint32 bank = index / 32;
        uint32 ci = index % 32;
        // High nibbles
        custom[0x106/2] = (uint16)(bank << 13);
        uint16 hi = ((uint16)(c.r & 0xF0) << 4) |
                    ((uint16)(c.g & 0xF0)) |
                    ((uint16)(c.b & 0xF0) >> 4);
        custom[(0x180 + ci * 2) / 2] = hi;
        // Low nibbles
        custom[0x106/2] = (uint16)((bank << 13) | 0x0200);
        uint16 lo = ((uint16)(c.r & 0x0F) << 8) |
                    ((uint16)(c.g & 0x0F) << 4) |
                    ((uint16)(c.b & 0x0F));
        custom[(0x180 + ci * 2) / 2] = lo;
    }
}

Color get_entry(uint32 index) {
    if (index >= INODE_SIZE) return Color(0, 0, 0);
    return g_aga_palette[index];
}

void generate_default() {
    // Regenerate the default 6x6x6 + 40 grays palette
    setup_aga_palette();
}

void generate_glass_remap(uint8* remap_table, const Color& tint, uint8 alpha) {
    if (!remap_table) return;
    uint32 a = alpha;
    uint32 inv_a = 255 - alpha;
    for (int i = 0; i < INODE_SIZE; i++) {
        int32 tr = ((int32)g_aga_palette[i].r * inv_a + (int32)tint.r * a) >> 8;
        int32 tg = ((int32)g_aga_palette[i].g * inv_a + (int32)tint.g * a) >> 8;
        int32 tb = ((int32)g_aga_palette[i].b * inv_a + (int32)tint.b * a) >> 8;
        tr = clamp_i32(tr, 0, 255);
        tg = clamp_i32(tg, 0, 255);
        tb = clamp_i32(tb, 0, 255);
        // Find nearest palette entry
        int best = 0;
        int32 best_dist = 0x7FFFFFFF;
        for (int j = 0; j < INODE_SIZE; j++) {
            int32 dr = tr - (int32)g_aga_palette[j].r;
            int32 dg = tg - (int32)g_aga_palette[j].g;
            int32 db = tb - (int32)g_aga_palette[j].b;
            int32 dist = dr * dr + dg * dg + db * db;
            if (dist < best_dist) {
                best_dist = dist;
                best = j;
            }
        }
        remap_table[i] = (uint8)best;
    }
}

} // namespace palette

} // namespace gui
} // namespace neo
