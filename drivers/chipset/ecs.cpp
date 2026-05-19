/*
 * NeoBench Bare-Metal Amiga Kernel
 * ECS/OCS Display Driver
 *
 * Provides 80x32 text console on 640xINODE_SIZE (PAL) or 640x200 (NTSC)
 * hires display with 4 colors (2 bitplanes). Uses built-in 8x8 font
 * and blitter-accelerated character rendering.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "custom.h"

namespace neo {
namespace ecs {

/* ========================================================================
 * Configuration
 * ======================================================================== */

static constexpr int SCREEN_WIDTH    = 640;
static constexpr int SCREEN_HEIGHT_PAL  = INODE_SIZE;
static constexpr int SCREEN_HEIGHT_NTSC = 200;

static constexpr int FONT_WIDTH      = 8;
static constexpr int FONT_HEIGHT     = 8;
static constexpr int COLS            = 80;   /* 640 / 8 */
static constexpr int ROWS_PAL        = 32;   /* INODE_SIZE / 8 */
static constexpr int ROWS_NTSC       = 25;   /* 200 / 8 */

static constexpr int NUM_BITPLANES   = 3;   /* 8 colors */
static constexpr int BYTES_PER_ROW   = SCREEN_WIDTH / 8;  /* 80 bytes */

/* Maximum copper list size in 16-bit words */
static constexpr int COPLIST_SIZE    = INODE_SIZE;

/* ========================================================================
 * State
 * ======================================================================== */

static int screen_height;
static int rows;
static int cursor_x;
static int cursor_y;
static bool cursor_visible;
static int vblank_counter;

/* Bitplane memory - must be in Chip RAM (first 2MB) */
static uint8_t *bitplane[NUM_BITPLANES];
static uint16_t *copperlist;
static uint32_t plane_size;

/* Null sprite for disabling sprites */
static uint32_t null_sprite[2] __attribute__((aligned(4)));

/* ========================================================================
 * Built-in 8x8 Bitmap Font (ASCII 32-126, 95 characters)
 *
 * Each character is 8 bytes (8 rows of 8 pixels, MSB = leftmost pixel).
 * ======================================================================== */

static const uint8_t font_8x8[95][8] = {
    /* 32: Space */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 33: ! */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 },
    /* 34: " */
    { 0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 35: # */
    { 0x24, 0x24, 0x7E, 0x24, 0x7E, 0x24, 0x24, 0x00 },
    /* 36: $ */
    { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 },
    /* 37: % */
    { 0x62, 0x64, 0x08, 0x10, 0x20, 0x4C, 0x8C, 0x00 },
    /* 38: & */
    { 0x30, 0x48, 0x48, 0x30, 0x4A, 0x44, 0x3A, 0x00 },
    /* 39: ' */
    { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 40: ( */
    { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 },
    /* 41: ) */
    { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 },
    /* 42: * */
    { 0x00, 0x24, 0x18, 0x7E, 0x18, 0x24, 0x00, 0x00 },
    /* 43: + */
    { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 },
    /* 44: , */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 },
    /* 45: - */
    { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },
    /* 46: . */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },
    /* 47: / */
    { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00 },
    /* 48: 0 */
    { 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x42, 0x3C, 0x00 },
    /* 49: 1 */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    /* 50: 2 */
    { 0x3C, 0x42, 0x02, 0x0C, 0x30, 0x40, 0x7E, 0x00 },
    /* 51: 3 */
    { 0x3C, 0x42, 0x02, 0x1C, 0x02, 0x42, 0x3C, 0x00 },
    /* 52: 4 */
    { 0x0C, 0x14, 0x24, 0x44, 0x7E, 0x04, 0x04, 0x00 },
    /* 53: 5 */
    { 0x7E, 0x40, 0x7C, 0x02, 0x02, 0x42, 0x3C, 0x00 },
    /* 54: 6 */
    { 0x1C, 0x20, 0x40, 0x7C, 0x42, 0x42, 0x3C, 0x00 },
    /* 55: 7 */
    { 0x7E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00 },
    /* 56: 8 */
    { 0x3C, 0x42, 0x42, 0x3C, 0x42, 0x42, 0x3C, 0x00 },
    /* 57: 9 */
    { 0x3C, 0x42, 0x42, 0x3E, 0x02, 0x04, 0x38, 0x00 },
    /* 58: : */
    { 0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x00, 0x00 },
    /* 59: ; */
    { 0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30 },
    /* 60: < */
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 },
    /* 61: = */
    { 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00 },
    /* 62: > */
    { 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00 },
    /* 63: ? */
    { 0x3C, 0x42, 0x02, 0x0C, 0x18, 0x00, 0x18, 0x00 },
    /* 64: @ */
    { 0x3C, 0x42, 0x4E, 0x52, 0x4E, 0x40, 0x3C, 0x00 },
    /* 65: A */
    { 0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00 },
    /* 66: B */
    { 0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00 },
    /* 67: C */
    { 0x3C, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3C, 0x00 },
    /* 68: D */
    { 0x78, 0x44, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00 },
    /* 69: E */
    { 0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00 },
    /* 70: F */
    { 0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00 },
    /* 71: G */
    { 0x3C, 0x42, 0x40, 0x4E, 0x42, 0x42, 0x3E, 0x00 },
    /* 72: H */
    { 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00 },
    /* 73: I */
    { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    /* 74: J */
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x46, 0x3C, 0x00 },
    /* 75: K */
    { 0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00 },
    /* 76: L */
    { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00 },
    /* 77: M */
    { 0x42, 0x66, 0x5A, 0x5A, 0x42, 0x42, 0x42, 0x00 },
    /* 78: N */
    { 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00 },
    /* 79: O */
    { 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00 },
    /* 80: P */
    { 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00 },
    /* 81: Q */
    { 0x3C, 0x42, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00 },
    /* 82: R */
    { 0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00 },
    /* 83: S */
    { 0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00 },
    /* 84: T */
    { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },
    /* 85: U */
    { 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00 },
    /* 86: V */
    { 0x42, 0x42, 0x42, 0x42, 0x24, 0x24, 0x18, 0x00 },
    /* 87: W */
    { 0x42, 0x42, 0x42, 0x5A, 0x5A, 0x66, 0x42, 0x00 },
    /* 88: X */
    { 0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00 },
    /* 89: Y */
    { 0x42, 0x42, 0x24, 0x18, 0x18, 0x18, 0x18, 0x00 },
    /* 90: Z */
    { 0x7E, 0x02, 0x04, 0x18, 0x20, 0x40, 0x7E, 0x00 },
    /* 91: [ */
    { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 },
    /* 92: \ */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00 },
    /* 93: ] */
    { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 },
    /* 94: ^ */
    { 0x18, 0x24, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 95: _ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00 },
    /* 96: ` */
    { 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 97: a */
    { 0x00, 0x00, 0x3C, 0x02, 0x3E, 0x42, 0x3E, 0x00 },
    /* 98: b */
    { 0x40, 0x40, 0x5C, 0x62, 0x42, 0x62, 0x5C, 0x00 },
    /* 99: c */
    { 0x00, 0x00, 0x3C, 0x42, 0x40, 0x42, 0x3C, 0x00 },
    /* 100: d */
    { 0x02, 0x02, 0x3A, 0x46, 0x42, 0x46, 0x3A, 0x00 },
    /* 101: e */
    { 0x00, 0x00, 0x3C, 0x42, 0x7E, 0x40, 0x3C, 0x00 },
    /* 102: f */
    { 0x0C, 0x12, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x00 },
    /* 103: g */
    { 0x00, 0x00, 0x3A, 0x46, 0x46, 0x3A, 0x02, 0x3C },
    /* 104: h */
    { 0x40, 0x40, 0x5C, 0x62, 0x42, 0x42, 0x42, 0x00 },
    /* 105: i */
    { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    /* 106: j */
    { 0x06, 0x00, 0x06, 0x06, 0x06, 0x46, 0x46, 0x3C },
    /* 107: k */
    { 0x40, 0x40, 0x44, 0x48, 0x70, 0x48, 0x44, 0x00 },
    /* 108: l */
    { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    /* 109: m */
    { 0x00, 0x00, 0x76, 0x4A, 0x4A, 0x4A, 0x4A, 0x00 },
    /* 110: n */
    { 0x00, 0x00, 0x5C, 0x62, 0x42, 0x42, 0x42, 0x00 },
    /* 111: o */
    { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00 },
    /* 112: p */
    { 0x00, 0x00, 0x5C, 0x62, 0x62, 0x5C, 0x40, 0x40 },
    /* 113: q */
    { 0x00, 0x00, 0x3A, 0x46, 0x46, 0x3A, 0x02, 0x02 },
    /* 114: r */
    { 0x00, 0x00, 0x5C, 0x62, 0x40, 0x40, 0x40, 0x00 },
    /* 115: s */
    { 0x00, 0x00, 0x3E, 0x40, 0x3C, 0x02, 0x7C, 0x00 },
    /* 116: t */
    { 0x10, 0x10, 0x7C, 0x10, 0x10, 0x12, 0x0C, 0x00 },
    /* 117: u */
    { 0x00, 0x00, 0x42, 0x42, 0x42, 0x46, 0x3A, 0x00 },
    /* 118: v */
    { 0x00, 0x00, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00 },
    /* 119: w */
    { 0x00, 0x00, 0x42, 0x42, 0x5A, 0x5A, 0x24, 0x00 },
    /* 120: x */
    { 0x00, 0x00, 0x42, 0x24, 0x18, 0x24, 0x42, 0x00 },
    /* 121: y */
    { 0x00, 0x00, 0x42, 0x42, 0x46, 0x3A, 0x02, 0x3C },
    /* 122: z */
    { 0x00, 0x00, 0x7E, 0x04, 0x18, 0x20, 0x7E, 0x00 },
    /* 123: { */
    { 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00 },
    /* 124: | */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },
    /* 125: } */
    { 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00 },
    /* 126: ~ */
    { 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00 }
};

/* ========================================================================
 * Forward declarations
 * ======================================================================== */

static void build_copperlist(void);
static void render_char(int col, int row, uint8_t ch, uint8_t color);

/* ========================================================================
 * Memory allocation from Chip RAM
 * (We assume chip_alloc is provided by the kernel memory manager)
 * ======================================================================== */

extern "C" void *chip_alloc(uint32_t size, uint32_t alignment);

/* ========================================================================
 * Copper List Construction
 * ======================================================================== */

static void build_copperlist(void)
{
    int pos = 0;

    /* Set BPLCON0: hires, 3 bitplanes, color burst on */
    cop_move(copperlist, &pos, BPLCON0,
             BPLCON0_HIRES | BPLCON0_COLOR | (3 << 12));

    /* BPLCON1: no scroll */
    cop_move(copperlist, &pos, BPLCON1, 0x0000);

    /* BPLCON2: default priorities */
    cop_move(copperlist, &pos, BPLCON2, 0x0024);

    /* Display window */
    if (screen_height >= INODE_SIZE) {
        /* PAL: full height */
        cop_move(copperlist, &pos, DIWSTRT, 0x2C81);
        cop_move(copperlist, &pos, DIWSTOP, 0x2CC1);
    } else {
        /* NTSC */
        cop_move(copperlist, &pos, DIWSTRT, 0x2C81);
        cop_move(copperlist, &pos, DIWSTOP, 0xF4C1);
    }

    /* Data fetch start/stop for hires */
    cop_move(copperlist, &pos, DDFSTRT, 0x003C);
    cop_move(copperlist, &pos, DDFSTOP, 0x00D4);

    /* Bitplane modulos (no interleave) */
    cop_move(copperlist, &pos, BPL1MOD, 0x0000);
    cop_move(copperlist, &pos, BPL2MOD, 0x0000);

    /* Bitplane pointers */
    uint32_t bpl0 = (uint32_t)bitplane[0];
    uint32_t bpl1 = (uint32_t)bitplane[1];
    uint32_t bpl2 = (uint32_t)bitplane[2];
    cop_move(copperlist, &pos, BPL1PTH, (uint16_t)(bpl0 >> 16));
    cop_move(copperlist, &pos, BPL1PTL, (uint16_t)(bpl0 & 0xFFFF));
    cop_move(copperlist, &pos, BPL2PTH, (uint16_t)(bpl1 >> 16));
    cop_move(copperlist, &pos, BPL2PTL, (uint16_t)(bpl1 & 0xFFFF));
    cop_move(copperlist, &pos, BPL3PTH, (uint16_t)(bpl2 >> 16));
    cop_move(copperlist, &pos, BPL3PTL, (uint16_t)(bpl2 & 0xFFFF));

    /* Colors: pure black background, bright white text */
    cop_move(copperlist, &pos, COLOR00, 0x0000);  /* Black (Background) */
    cop_move(copperlist, &pos, COLOR01, 0x0FFF);  /* White (Text) */
    cop_move(copperlist, &pos, COLOR02, 0x00B0);  /* Cyan (INFO) */
    cop_move(copperlist, &pos, COLOR03, 0x00F0);  /* Green (OK) */
    cop_move(copperlist, &pos, COLOR04, 0x0FB0);  /* Amber/Yellow (WARN) */
    cop_move(copperlist, &pos, COLOR05, 0x0F00);  /* Red (FAIL) */
    cop_move(copperlist, &pos, COLOR06, 0x0A0F);  /* Purple */
    cop_move(copperlist, &pos, COLOR07, 0x0888);  /* Grey */

    /* Disable all sprites */
    for (int i = 0; i < 8; i++) {
        uint32_t ns = (uint32_t)&null_sprite[0];
        cop_move(copperlist, &pos, SPR0PTH + (i * 4),
                 (uint16_t)(ns >> 16));
        cop_move(copperlist, &pos, SPR0PTH + (i * 4) + 2,
                 (uint16_t)(ns & 0xFFFF));
    }

    /* End of copper list */
    cop_end(copperlist, &pos);
}

/* ========================================================================
 * Character Rendering (Blitter-accelerated)
 * ======================================================================== */

static void render_char(int col, int row, uint8_t ch, uint8_t color)
{
    if (ch < 32 || ch > 126) ch = 32;

    const uint8_t *glyph = font_8x8[ch - 32];
    int byte_offset = col;  /* Each char is 1 byte wide in hires */
    int line_offset = row * FONT_HEIGHT * BYTES_PER_ROW;

    for (int y = 0; y < FONT_HEIGHT; y++) {
        int offset = line_offset + (y * BYTES_PER_ROW) + byte_offset;
        uint8_t bits = glyph[y];

        /* Plane 0: bit 0 of color */
        bitplane[0][offset] = (color & 0x01) ? bits : 0x00;
        /* Plane 1: bit 1 of color */
        bitplane[1][offset] = (color & 0x02) ? bits : 0x00;
        /* Plane 2: bit 2 of color */
        bitplane[2][offset] = (color & 0x04) ? bits : 0x00;
    }
}

/* Blitter-based character rendering for faster output */
static void render_char_blit(int col, int row, uint8_t ch, uint8_t color)
{
    if (ch < 32 || ch > 126) ch = 32;

    const uint8_t *glyph = font_8x8[ch - 32];
    uint32_t dest_offset = (uint32_t)(row * FONT_HEIGHT * BYTES_PER_ROW + col);

    wait_blit();

    for (int plane = 0; plane < NUM_BITPLANES; plane++) {
        uint32_t dest_addr = (uint32_t)bitplane[plane] + dest_offset;

        if (color & (1 << plane)) {
            /* Use blitter to copy font data: A -> D with minterm A (0xF0) */
            custom_write(BLTCON0, 0x09F0);  /* Use A and D, minterm = A */
            custom_write(BLTCON1, 0x0000);
            custom_write(BLTAFWM, 0xFFFF);
            custom_write(BLTALWM, 0xFFFF);
            custom_write32(BLTAPTH, (uint32_t)glyph);
            custom_write(BLTAMOD, 0x0000);  /* Font data is contiguous */
            custom_write32(BLTDPTH, dest_addr);
            custom_write(BLTDMOD, BYTES_PER_ROW - 1);  /* Skip to next row */
            /* BLT size: 8 rows, 1 word wide (but we only need 1 byte) */
            custom_write(BLTSIZE, (8 << 6) | 1);
            wait_blit();
        } else {
            /* Clear this character cell in this plane */
            custom_write(BLTCON0, 0x0100);  /* D only, minterm = 0 */
            custom_write(BLTCON1, 0x0000);
            custom_write32(BLTDPTH, dest_addr);
            custom_write(BLTDMOD, BYTES_PER_ROW - 1);
            custom_write(BLTSIZE, (8 << 6) | 1);
            wait_blit();
        }
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init_ecs(void)
{
    /* Detect PAL/NTSC */
    bool pal = is_pal();
    screen_height = pal ? SCREEN_HEIGHT_PAL : SCREEN_HEIGHT_NTSC;
    rows = pal ? ROWS_PAL : ROWS_NTSC;

    /* Calculate bitplane size */
    plane_size = BYTES_PER_ROW * screen_height;

    /* Allocate chip RAM for bitplanes and copper list */
    bitplane[0] = (uint8_t *)chip_alloc(plane_size, 8);
    bitplane[1] = (uint8_t *)chip_alloc(plane_size, 8);
    copperlist  = (uint16_t *)chip_alloc(COPLIST_SIZE * sizeof(uint16_t), 4);

    if (!bitplane[0] || !bitplane[1] || !copperlist) {
        return false;
    }

    /* Clear bitplanes */
    for (uint32_t i = 0; i < plane_size; i++) {
        bitplane[0][i] = 0;
        bitplane[1][i] = 0;
    }

    /* Initialize null sprite */
    null_sprite[0] = 0;
    null_sprite[1] = 0;

    /* Reset cursor */
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = true;
    vblank_counter = 0;

    /* Disable all DMA first */
    custom_write(DMACON, 0x7FFF);

    /* Build copper list */
    build_copperlist();

    /* Point copper to our list */
    custom_write32(COP1LCH, (uint32_t)copperlist);

    /* Strobe COPJMP1 to start copper */
    custom_write(COPJMP1, 0);

    /* Enable DMA: master, copper, raster, blitter */
    custom_write(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER |
                         DMAF_RASTER | DMAF_BLITTER);

    return true;
}

void clear_ecs(void)
{
    /* Clear both bitplanes using blitter */
    wait_blit();

    for (int plane = 0; plane < NUM_BITPLANES; plane++) {
        custom_write(BLTCON0, 0x0100);  /* D only, clear */
        custom_write(BLTCON1, 0x0000);
        custom_write32(BLTDPTH, (uint32_t)bitplane[plane]);
        custom_write(BLTDMOD, 0x0000);
        /* Total words = plane_size / 2 */
        uint16_t total_words = (uint16_t)(plane_size / 2);
        uint16_t h = total_words / 40;  /* 40 words per row in hires */
        custom_write(BLTSIZE, (h << 6) | 40);
        wait_blit();
    }

    cursor_x = 0;
    cursor_y = 0;
}

void scroll_ecs(void)
{
    /* Scroll up one line (FONT_HEIGHT pixels) using blitter */
    uint32_t row_bytes = FONT_HEIGHT * BYTES_PER_ROW;

    for (int plane = 0; plane < NUM_BITPLANES; plane++) {
        /* Copy rows 1..N-1 to rows 0..N-2 */
        wait_blit();
        custom_write(BLTCON0, 0x09F0);  /* A -> D copy */
        custom_write(BLTCON1, 0x0000);
        custom_write(BLTAFWM, 0xFFFF);
        custom_write(BLTALWM, 0xFFFF);
        custom_write32(BLTAPTH, (uint32_t)bitplane[plane] + row_bytes);
        custom_write(BLTAMOD, 0x0000);
        custom_write32(BLTDPTH, (uint32_t)bitplane[plane]);
        custom_write(BLTDMOD, 0x0000);

        uint32_t copy_size = plane_size - row_bytes;
        uint16_t words = (uint16_t)(copy_size / 2);
        uint16_t h = words / 40;
        custom_write(BLTSIZE, (h << 6) | 40);
        wait_blit();

        /* Clear the last row */
        uint32_t last_row_addr = (uint32_t)bitplane[plane] + plane_size - row_bytes;
        custom_write(BLTCON0, 0x0100);
        custom_write(BLTCON1, 0x0000);
        custom_write32(BLTDPTH, last_row_addr);
        custom_write(BLTDMOD, 0x0000);
        uint16_t clear_words = (uint16_t)(row_bytes / 2);
        uint16_t ch = clear_words / 40;
        custom_write(BLTSIZE, (ch << 6) | 40);
        wait_blit();
    }
}

void putchar_ecs(char c, uint8_t color)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            render_char(cursor_x, cursor_y, ' ', 0);
        }
    } else if (c == '\t') {
        int next_tab = (cursor_x + 8) & ~7;
        while (cursor_x < next_tab && cursor_x < COLS) {
            render_char(cursor_x, cursor_y, ' ', 0);
            cursor_x++;
        }
    } else {
        /* Render the character in the specified color */
        render_char(cursor_x, cursor_y, (uint8_t)c, color);
        cursor_x++;
    }

    /* Wrap at end of line */
    if (cursor_x >= COLS) {
        cursor_x = 0;
        cursor_y++;
    }

    /* Scroll if needed */
    if (cursor_y >= rows) {
        scroll_ecs();
        cursor_y = rows - 1;
    }
}

void puts_ecs(const char *str, uint8_t color)
{
    while (*str) {
        putchar_ecs(*str++, color);
    }
}

void set_color_ecs(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < 0 || index > 31) return;
    set_color_ocs(index, r >> 4, g >> 4, b >> 4);
}

void cursor_blink_vblank(void)
{
    vblank_counter++;
    if (vblank_counter >= 25) {  /* Toggle every ~0.5s at 50Hz PAL */
        vblank_counter = 0;
        cursor_visible = !cursor_visible;

        /* Draw or erase cursor block at current position */
        if (cursor_visible) {
            /* Draw a solid block as cursor (color 3 = highlight) */
            render_char(cursor_x, cursor_y, 0x7F, 3);
        } else {
            /* Erase cursor (show space) */
            render_char(cursor_x, cursor_y, ' ', 0);
        }
    }
}

/* Accessors */
int get_cols(void)   { return COLS; }
int get_rows(void)   { return rows; }
int get_cursor_x(void) { return cursor_x; }
int get_cursor_y(void) { return cursor_y; }
void set_cursor(int x, int y) { cursor_x = x; cursor_y = y; }

const uint8_t *get_font(void)
{
    return &font_8x8[0][0];
}

} /* namespace ecs */
} /* namespace neo */
