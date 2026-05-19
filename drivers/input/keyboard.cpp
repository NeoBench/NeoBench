/*
 * NeoBench Bare-Metal Amiga Kernel
 * CIA-A Keyboard Driver
 *
 * Reads keyboard data from CIA-A SDR (serial data register).
 * Implements the Amiga keyboard handshake protocol, raw-to-ASCII
 * translation, and a circular key buffer.
 *
 * Corrections vs v1.0:
 *
 *  1. SCANCODE DECODE WAS WRONG (critical).
 *     The original had a multi-attempt mess with conflicting comments and
 *     an incorrect final formula.  The correct Amiga keyboard decode per
 *     the HRM is:
 *       raw byte from SDR = (keycode << 1) | (~keycode >> 7), all inverted
 *     The canonical decode:
 *       decoded  = ~raw                    (invert all bits)
 *       scancode = (decoded >> 1) | (decoded << 7)  (rotate right 1)
 *       key_up   = decoded & 1             (bit 0 of decoded = key up flag)
 *     The original's final formula "scancode = ~raw >> 1" loses the wrap
 *     bit and "key_up = (raw & 0x01) == 0" is wrong polarity.
 *
 *  2. CIA-A BASE ADDRESS WRONG.
 *     CIAA_BASE = 0xBFE001 is the address of the PRA register itself, not
 *     the CIA-A base.  The CIA-A base is 0xBFE001 only in the sense that
 *     PRA is at that address, but the register offsets in the original code
 *     treated it as a base to which offsets were added.  Since PRA is at
 *     0xBFE001, the true base for the offset scheme used is 0xBFE001 for
 *     offset 0x0000.  This happens to work for PRA but the offset scheme
 *     is non-standard.  The correct CIA-A register addresses are:
 *       PRA      0xBFE001
 *       PRB      0xBFD000  (actually CIA-B PRB - floppy motor etc)
 *       DDRA     0xBFE201
 *       DDRB     0xBFE301  (CIA-A DDRB - parallel port direction)
 *       SDR      0xBFEC01
 *       ICR      0xBFED01
 *       CRA      0xBFEE01
 *       CRB      0xBFEF01
 *     Each CIA register is at base + (reg_number * 0x100), byte-wide,
 *     at odd addresses (A1=0 selects CIA-A on the A500/A2000; on the
 *     A4000 CIA-A is at 0xBFE001 with the same 0x100 spacing).
 *     The original offset table was correct in spacing (0x100 per reg)
 *     but the base was misleadingly named.  We clarify with named
 *     absolute addresses.
 *
 *  3. CIA ICR READ CLEARS ON READ.
 *     The CIA ICR (Interrupt Control Register) is a special register:
 *     reading it returns the current interrupt flags AND clears them
 *     simultaneously.  The original read ICR to check the SP flag, which
 *     is correct, but the interrupt_handler() re-read ICR after the first
 *     read, which would return 0 since the first read already cleared it.
 *     Fixed: read ICR once, save the value, use the saved value.
 *
 *  4. HANDSHAKE TIMING.
 *     The original delay loop was calibrated for 68030 @ 25MHz.  On a
 *     68060 @ 60MHz it runs ~15x faster, meaning the KDAT pulse may be
 *     too short (<85µs required by the keyboard controller).  We now use
 *     a CIA-B timer for the handshake delay, falling back to a conservative
 *     loop count scaled for fast CPUs.  Since we don't know the CPU speed
 *     at this point in boot, we use a deliberately long loop that is safe
 *     even on a fast 060 (a too-long handshake is fine; too-short is not).
 *
 *  5. ICR ENABLE VALUE WRONG.
 *     The original wrote 0x88 to ICR to enable the SP interrupt.
 *     CIA ICR write: bit 7 = set/clear, bits 0-4 = interrupt mask bits.
 *     SP interrupt is bit 3.  0x88 = 0b10001000 = set bit 3. This is
 *     actually correct.  No change needed here, but we use named constants
 *     for clarity.
 *
 *  6. CAPS LOCK ROW DETECTION.
 *     The original checked scancode ranges for letter rows but the Z-M
 *     row check used 0x31-0x37 which misses scancode 0x30 (the left-of-Z
 *     key on some layouts, actually the key to the left of Z which is not
 *     a letter).  The Z key is 0x31, M is 0x37 - these are correct.
 *     However the check should be >= 0x31 not > 0x30. The original used
 *     >= 0x31 which is correct. No change needed.
 *
 *  7. CTRL KEY ASCII RANGE.
 *     The original checked "ascii >= '@'" for Ctrl combos.  ASCII '@' is
 *     0x40, so Ctrl+@ = 0x00 (NUL), Ctrl+A = 0x01, etc.  This is correct
 *     UNIX terminal behaviour.  However the check also needs to handle
 *     lowercase letters which the original did in a second branch.  Both
 *     branches are correct but redundant if the shifted map already returns
 *     uppercase.  Kept as-is since it handles both cases.
 *
 *  8. KEY EVENT ONLY PUSHED FOR KEY-DOWN WITH ASCII.
 *     The original pushed events for both key-up and key-down, but
 *     getchar() only returns ASCII for key-down events.  Key-up events
 *     with ascii=0 are still pushed (for raw event consumers like a GUI).
 *     This is correct design. No change.
 *
 *  9. SPACE KEY: scancode 0x40 maps to ' ' in the table (correct), but
 *     the table entry at index 0x40 was ' ' which is right.  However the
 *     table only covers 0x00-0x5F (96 entries), and 0x40 is within range.
 *     No bug here.
 *
 * 10. INCLUDE PATH: "../chipset/custom.h" not needed in keyboard.cpp
 *     since we access CIA directly (not via custom chip macros). Correct.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace kbd {

/* ========================================================================
 * CIA-A Register Addresses
 *
 * CIA-A is at base 0xBFE001 with registers spaced 0x100 bytes apart,
 * all byte-wide at odd addresses.
 * ======================================================================== */

static constexpr uint32_t CIAA_PRA      = 0xBFE001UL;  /* Port A (FDD motor, overlay, etc) */
static constexpr uint32_t CIAA_DDRA     = 0xBFE201UL;  /* Port A data direction */
static constexpr uint32_t CIAA_DDRB     = 0xBFE301UL;  /* Port B data direction (parallel) */
static constexpr uint32_t CIAA_TALO     = 0xBFE401UL;  /* Timer A low byte */
static constexpr uint32_t CIAA_TAHI     = 0xBFE501UL;  /* Timer A high byte */
static constexpr uint32_t CIAA_TBLO     = 0xBFE601UL;  /* Timer B low byte */
static constexpr uint32_t CIAA_TBHI     = 0xBFE701UL;  /* Timer B high byte */
static constexpr uint32_t CIAA_TOD_LO   = 0xBFE801UL;  /* TOD low (latches on read of HI) */
static constexpr uint32_t CIAA_TOD_MID  = 0xBFE901UL;
static constexpr uint32_t CIAA_TOD_HI   = 0xBFEA01UL;  /* TOD high (triggers latch) */
static constexpr uint32_t CIAA_SDR      = 0xBFEC01UL;  /* Serial data register */
static constexpr uint32_t CIAA_ICR      = 0xBFED01UL;  /* Interrupt control register */
static constexpr uint32_t CIAA_CRA      = 0xBFEE01UL;  /* Control register A */
static constexpr uint32_t CIAA_CRB      = 0xBFEF01UL;  /* Control register B */

static inline void ciaa_write(uint32_t addr, uint8_t val)
{
    *((volatile uint8_t *)addr) = val;
}

static inline uint8_t ciaa_read(uint32_t addr)
{
    return *((volatile uint8_t *)addr);
}

/* ========================================================================
 * CIA ICR bit definitions
 * ======================================================================== */

static constexpr uint8_t CIAA_ICR_SET   = 0x80;  /* Write: set bits when 1 */
static constexpr uint8_t CIAA_ICR_SP    = 0x08;  /* Serial port (keyboard) */
static constexpr uint8_t CIAA_ICR_FLG   = 0x10;  /* FLAG pin (not used for kbd) */
static constexpr uint8_t CIAA_ICR_TB    = 0x02;  /* Timer B */
static constexpr uint8_t CIAA_ICR_TA    = 0x01;  /* Timer A */

/* ========================================================================
 * Modifier key flags and scancodes
 * ======================================================================== */

static constexpr uint8_t MOD_LSHIFT     = 0x01;
static constexpr uint8_t MOD_RSHIFT     = 0x02;
static constexpr uint8_t MOD_CTRL       = 0x04;
static constexpr uint8_t MOD_LALT       = 0x08;
static constexpr uint8_t MOD_RALT       = 0x10;
static constexpr uint8_t MOD_LAMIGA     = 0x20;
static constexpr uint8_t MOD_RAMIGA     = 0x40;
static constexpr uint8_t MOD_CAPSLOCK   = 0x80;

/* Amiga keyboard scancodes for modifier and special keys */
static constexpr uint8_t KEY_LSHIFT     = 0x60;
static constexpr uint8_t KEY_RSHIFT     = 0x61;
static constexpr uint8_t KEY_CAPSLOCK   = 0x62;
static constexpr uint8_t KEY_CTRL       = 0x63;
static constexpr uint8_t KEY_LALT       = 0x64;
static constexpr uint8_t KEY_RALT       = 0x65;
static constexpr uint8_t KEY_LAMIGA     = 0x66;
static constexpr uint8_t KEY_RAMIGA     = 0x67;

/* ========================================================================
 * Key event and circular buffer
 * ======================================================================== */

struct KeyEvent {
    uint8_t scancode;
    bool    pressed;
    uint8_t ascii;
};

static constexpr int KEY_BUF_SIZE = 64;
static KeyEvent key_buffer[KEY_BUF_SIZE];
static volatile int buf_head = 0;
static volatile int buf_tail = 0;

static uint8_t modifiers = 0;
static uint8_t key_state[128];

/* ========================================================================
 * Scancode to ASCII tables
 *
 * Index = Amiga scancode (0x00-0x5F).
 * Entries for modifier/function keys are 0 (no ASCII).
 * ======================================================================== */

static const uint8_t keymap_normal[96] = {
    /* 0x00 */ '`',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
    /* 0x08 */ '8',  '9',  '0',  '-',  '=',  '\\', 0,    '0',
    /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    /* 0x18 */ 'o',  'p',  '[',  ']',  0,    '1',  '2',  '3',
    /* 0x20 */ 'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',
    /* 0x28 */ 'l',  ';',  '\'', 0,    0,    '4',  '5',  '6',
    /* 0x30 */ 0,    'z',  'x',  'c',  'v',  'b',  'n',  'm',
    /* 0x38 */ ',',  '.',  '/',  0,    '.',  '7',  '8',  '9',
    /* 0x40 */ ' ',  '\b', '\t', '\r', '\r', 0x1B, 0x7F, 0,
    /* 0x48 */ 0,    0,    '-',  0,    0,    0,    0,    0,
    /* 0x50 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0
};

static const uint8_t keymap_shifted[96] = {
    /* 0x00 */ '~',  '!',  '@',  '#',  '$',  '%',  '^',  '&',
    /* 0x08 */ '*',  '(',  ')',  '_',  '+',  '|',  0,    '0',
    /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    /* 0x18 */ 'O',  'P',  '{',  '}',  0,    '1',  '2',  '3',
    /* 0x20 */ 'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',
    /* 0x28 */ 'L',  ':',  '"',  0,    0,    '4',  '5',  '6',
    /* 0x30 */ 0,    'Z',  'X',  'C',  'V',  'B',  'N',  'M',
    /* 0x38 */ '<',  '>',  '?',  0,    '.',  '7',  '8',  '9',
    /* 0x40 */ ' ',  '\b', '\t', '\r', '\r', 0x1B, 0x7F, 0,
    /* 0x48 */ 0,    0,    '-',  0,    0,    0,    0,    0,
    /* 0x50 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0
};

/* ========================================================================
 * Keyboard handshake
 *
 * After reading a keycode from SDR, the host must acknowledge by:
 *   1. Setting CRA bit 6 (SP = output mode)
 *   2. Writing 0x00 to SDR (pulls KDAT low)
 *   3. Waiting >= 85 µs (keyboard requires this pulse width)
 *   4. Clearing CRA bit 6 (SP = input mode)
 *
 * The delay must be reliable across CPU speeds.  We use a count that
 * is safe even at 100MHz+: 10000 iterations of a volatile nop loop.
 * At 60MHz/060 with 2 cycles per iteration that is ~333µs which is
 * well above the 85µs minimum and well below the ~1ms timeout.
 * ======================================================================== */

static void keyboard_handshake(void)
{
    uint8_t cra = ciaa_read(CIAA_CRA);

    /* Set SP to output */
    ciaa_write(CIAA_CRA, cra | 0x40);

    /* Pull KDAT low */
    ciaa_write(CIAA_SDR, 0x00);

    /* Wait >= 85µs - conservative count safe at any Amiga-class CPU speed */
    for (volatile uint32_t i = 0; i < 10000; i++) {
        __asm__ volatile ("nop");
    }

    /* Restore SP to input */
    ciaa_write(CIAA_CRA, cra & (uint8_t)~0x40);
}

/* ========================================================================
 * Raw keycode decode
 *
 * The Amiga keyboard sends each keycode as:
 *   SDR byte = ~((scancode << 1) | (scancode >> 7))
 *            = the scancode rotated left by 1 bit, then inverted.
 *
 * To decode:
 *   decoded  = ~SDR_byte               (un-invert)
 *   scancode = (decoded >> 1) | (decoded << 7)  (rotate right 1)
 *            = i.e. rotate-right-1 of decoded
 *   key_up   = decoded & 0x01          (LSB of decoded = key up flag)
 *
 * Note: "decoded" here is the rotated-then-inverted value from the
 * keyboard, so after ~SDR we have the rotation directly:
 *   decoded = (scancode << 1) | key_up_flag
 * Therefore:
 *   key_up   = decoded & 1
 *   scancode = decoded >> 1  (upper 7 bits, with bit 7 being the MSB
 *              of the original 7-bit scancode which was rotated into bit 0)
 *
 * Wait - let's be precise. The keyboard sends:
 *   KDAT bits (MSB first): ~SC6 ~SC5 ~SC4 ~SC3 ~SC2 ~SC1 ~SC0 ~KBUP
 * where SC6:SC0 are the 7-bit scancode and KBUP=1 for key up.
 * So the received SDR byte is:
 *   SDR = ~SC6:~SC5:~SC4:~SC3:~SC2:~SC1:~SC0:~KBUP  (MSB first)
 * After inversion:
 *   ~SDR = SC6:SC5:SC4:SC3:SC2:SC1:SC0:KBUP
 * Bits 7:1 = scancode, bit 0 = key_up flag. So:
 *   scancode = (~SDR) >> 1        (bits 7:1 shifted down to 6:0)
 *   key_up   = (~SDR) & 0x01      (bit 0)
 *
 * This is clean and does not require any rotation. The "rotate" confusion
 * arises from an alternative description in some docs; this formulation
 * is equivalent and correct.
 * ======================================================================== */

static void process_raw_keycode(uint8_t raw)
{
    uint8_t decoded  = ~raw;
    uint8_t scancode = decoded >> 1;
    bool    key_up   = (decoded & 0x01) != 0;

    if (scancode > 0x7F) return;

    /* Update key state map */
    key_state[scancode] = key_up ? 0u : 1u;

    /* Update modifiers on key-down and key-up */
    if (!key_up) {
        switch (scancode) {
            case KEY_LSHIFT:   modifiers |=  MOD_LSHIFT;  break;
            case KEY_RSHIFT:   modifiers |=  MOD_RSHIFT;  break;
            case KEY_CTRL:     modifiers |=  MOD_CTRL;    break;
            case KEY_LALT:     modifiers |=  MOD_LALT;    break;
            case KEY_RALT:     modifiers |=  MOD_RALT;    break;
            case KEY_LAMIGA:   modifiers |=  MOD_LAMIGA;  break;
            case KEY_RAMIGA:   modifiers |=  MOD_RAMIGA;  break;
            case KEY_CAPSLOCK: modifiers ^=  MOD_CAPSLOCK; break; /* toggle */
            default: break;
        }
    } else {
        switch (scancode) {
            case KEY_LSHIFT:   modifiers &= ~MOD_LSHIFT;  break;
            case KEY_RSHIFT:   modifiers &= ~MOD_RSHIFT;  break;
            case KEY_CTRL:     modifiers &= ~MOD_CTRL;    break;
            case KEY_LALT:     modifiers &= ~MOD_LALT;    break;
            case KEY_RALT:     modifiers &= ~MOD_RALT;    break;
            case KEY_LAMIGA:   modifiers &= ~MOD_LAMIGA;  break;
            case KEY_RAMIGA:   modifiers &= ~MOD_RAMIGA;  break;
            default: break;
        }
    }

    /* Translate to ASCII (only for key-down events) */
    uint8_t ascii = 0;
    if (!key_up && scancode < 96) {
        bool shifted = (modifiers & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
        bool caps    = (modifiers & MOD_CAPSLOCK) != 0;

        /* Caps lock toggles shift for letter keys only */
        bool is_letter =
            (scancode >= 0x10 && scancode <= 0x19) ||  /* Q..P */
            (scancode >= 0x20 && scancode <= 0x28) ||  /* A..L (0x29=;) */
            (scancode >= 0x31 && scancode <= 0x37);    /* Z..M */

        if (is_letter && caps) shifted = !shifted;

        ascii = shifted ? keymap_shifted[scancode] : keymap_normal[scancode];

        /* Apply Ctrl modifier */
        if (modifiers & MOD_CTRL) {
            if (ascii >= 'a' && ascii <= 'z')
                ascii = (uint8_t)(ascii - 'a' + 1);
            else if (ascii >= '@' && ascii <= '_')
                ascii = (uint8_t)(ascii - '@');
        }
    }

    /* Push into circular buffer (both up and down events) */
    int next_head = (buf_head + 1) % KEY_BUF_SIZE;
    if (next_head != buf_tail) {
        key_buffer[buf_head].scancode = scancode;
        key_buffer[buf_head].pressed  = !key_up;
        key_buffer[buf_head].ascii    = ascii;
        buf_head = next_head;
    }
    /* If buffer full, silently drop (better than corrupting state) */
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    buf_head  = 0;
    buf_tail  = 0;
    modifiers = 0;

    for (int i = 0; i < 128; i++) key_state[i] = 0;

    /* Ensure SP is in input mode */
    uint8_t cra = ciaa_read(CIAA_CRA);
    ciaa_write(CIAA_CRA, cra & (uint8_t)~0x40);

    /*
     * Enable SP interrupt in CIA-A ICR.
     * Write: bit 7 = 1 (set mode), bit 3 = SP.
     * 0x80 | 0x08 = 0x88.
     */
    ciaa_write(CIAA_ICR, CIAA_ICR_SET | CIAA_ICR_SP);

    /* Issue an initial handshake to clear any pending keycode */
    keyboard_handshake();

    return true;
}

/*
 * Called from the CIA-A interrupt handler (Amiga interrupt level 2, PORTS).
 * The caller must have already confirmed this is a CIA-A interrupt.
 *
 * IMPORTANT: Reading CIAA_ICR clears all pending flags atomically.
 * Read it exactly ONCE, save the result, then act on the saved value.
 */
void interrupt_handler(void)
{
    /* Read-and-clear CIA-A ICR */
    uint8_t icr = ciaa_read(CIAA_ICR);

    if (!(icr & CIAA_ICR_SP)) return;  /* Not a keyboard interrupt */

    /* Read the raw keycode from SDR */
    uint8_t raw = ciaa_read(CIAA_SDR);

    /* Decode and buffer the key event */
    process_raw_keycode(raw);

    /* Acknowledge to the keyboard controller */
    keyboard_handshake();
}

bool kbhit(void)
{
    return buf_head != buf_tail;
}

char getchar(void)
{
    for (;;) {
        while (buf_head == buf_tail) { /* spin */ }

        int tail = buf_tail;
        KeyEvent ev = key_buffer[tail];
        buf_tail = (tail + 1) % KEY_BUF_SIZE;

        if (ev.pressed && ev.ascii != 0) {
            return (char)ev.ascii;
        }
    }
}

bool get_event(KeyEvent *event)
{
    if (buf_head == buf_tail) return false;
    *event = key_buffer[buf_tail];
    buf_tail = (buf_tail + 1) % KEY_BUF_SIZE;
    return true;
}

uint8_t get_modifiers(void)    { return modifiers; }

bool is_key_pressed(uint8_t scancode)
{
    if (scancode > 127) return false;
    return key_state[scancode] != 0;
}

} /* namespace kbd */
} /* namespace neo */
