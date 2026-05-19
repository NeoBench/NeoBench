/*
 * NeoBench Bare-Metal Amiga Kernel
 * CIA-A Keyboard Driver (C99)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* CIA-A Register Addresses */
#define CIAA_PRA      0xBFE001UL
#define CIAA_SDR      0xBFEC01UL
#define CIAA_ICR      0xBFED01UL
#define CIAA_CRA      0xBFEE01UL

/* CIA ICR bits */
#define CIAA_ICR_SET   0x80
#define CIAA_ICR_SP    0x08

/* Modifiers */
#define MOD_LSHIFT     0x01
#define MOD_RSHIFT     0x02
#define MOD_CTRL       0x04
#define MOD_LALT       0x08
#define MOD_RALT       0x10
#define MOD_LAMIGA     0x20
#define MOD_RAMIGA     0x40
#define MOD_CAPSLOCK   0x80

#define KEY_LSHIFT     0x60
#define KEY_RSHIFT     0x61
#define KEY_CAPSLOCK   0x62
#define KEY_CTRL       0x63
#define KEY_LALT       0x64
#define KEY_RALT       0x65
#define KEY_LAMIGA     0x66
#define KEY_RAMIGA     0x67

typedef struct {
    uint8 scancode;
    uint8 pressed;
    uint8 ascii;
} KeyEvent;

#define KEY_BUF_SIZE 64
static KeyEvent key_buffer[KEY_BUF_SIZE];
static volatile int buf_head = 0;
static volatile int buf_tail = 0;

static uint8 modifiers = 0;
static uint8 key_state[128];

static const uint8 keymap_normal[96] = {
    '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, '0',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, '1', '2', '3',
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, '4', '5', '6',
    0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '.', '7', '8', '9',
    ' ', '\b', '\t', '\r', '\r', 0x1B, 0x7F, 0, 0, 0, '-', 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8 keymap_shifted[96] = {
    '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, '0',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, '1', '2', '3',
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, '4', '5', '6',
    0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '.', '7', '8', '9',
    ' ', '\b', '\t', '\r', '\r', 0x1B, 0x7F, 0, 0, 0, '-', 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static inline void ciaa_write(uint32 addr, uint8 val) { *(volatile uint8*)addr = val; }
static inline uint8 ciaa_read(uint32 addr) { return *(volatile uint8*)addr; }

static void keyboard_handshake(void) {
    uint8 cra = ciaa_read(CIAA_CRA);
    ciaa_write(CIAA_CRA, cra | 0x40);
    ciaa_write(CIAA_SDR, 0x00);
    for (volatile uint32 i = 0; i < 10000; i++);
    ciaa_write(CIAA_CRA, cra & (uint8)~0x40);
}

static void process_raw_keycode(uint8 raw) {
    uint8 decoded = (uint8)~raw;
    uint8 scancode = decoded >> 1;
    uint8 key_up = (decoded & 0x01) != 0;
    if (scancode > 0x7F) return;
    key_state[scancode] = key_up ? 0 : 1;
    if (!key_up) {
        switch (scancode) {
            case KEY_LSHIFT: modifiers |= MOD_LSHIFT; break;
            case KEY_RSHIFT: modifiers |= MOD_RSHIFT; break;
            case KEY_CTRL:   modifiers |= MOD_CTRL; break;
            case KEY_LALT:   modifiers |= MOD_LALT; break;
            case KEY_RALT:   modifiers |= MOD_RALT; break;
            case KEY_LAMIGA: modifiers |= MOD_LAMIGA; break;
            case KEY_RAMIGA: modifiers |= MOD_RAMIGA; break;
            case KEY_CAPSLOCK: modifiers ^= MOD_CAPSLOCK; break;
        }
    } else {
        switch (scancode) {
            case KEY_LSHIFT: modifiers &= ~MOD_LSHIFT; break;
            case KEY_RSHIFT: modifiers &= ~MOD_RSHIFT; break;
            case KEY_CTRL:   modifiers &= ~MOD_CTRL; break;
            case KEY_LALT:   modifiers &= ~MOD_LALT; break;
            case KEY_RALT:   modifiers &= ~MOD_RALT; break;
            case KEY_LAMIGA: modifiers &= ~MOD_LAMIGA; break;
            case KEY_RAMIGA: modifiers &= ~MOD_RAMIGA; break;
        }
    }
    uint8 ascii = 0;
    if (!key_up && scancode < 96) {
        uint8 shifted = (modifiers & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
        uint8 caps = (modifiers & MOD_CAPSLOCK) != 0;
        uint8 is_letter = (scancode >= 0x10 && scancode <= 0x19) || (scancode >= 0x20 && scancode <= 0x28) || (scancode >= 0x31 && scancode <= 0x37);
        if (is_letter && caps) shifted = !shifted;
        ascii = shifted ? keymap_shifted[scancode] : keymap_normal[scancode];
        if (modifiers & MOD_CTRL) {
            if (ascii >= 'a' && ascii <= 'z') ascii = (uint8)(ascii - 'a' + 1);
            else if (ascii >= '@' && ascii <= '_') ascii = (uint8)(ascii - '@');
        }
    }
    int next_head = (buf_head + 1) % KEY_BUF_SIZE;
    if (next_head != buf_tail) {
        key_buffer[buf_head].scancode = scancode;
        key_buffer[buf_head].pressed = !key_up;
        key_buffer[buf_head].ascii = ascii;
        buf_head = next_head;
    }
}

uint8 kbd_init(void) {
    buf_head = 0; buf_tail = 0; modifiers = 0;
    for (int i = 0; i < 128; i++) key_state[i] = 0;
    uint8 cra = ciaa_read(CIAA_CRA);
    ciaa_write(CIAA_CRA, cra & (uint8)~0x40);
    ciaa_write(CIAA_ICR, CIAA_ICR_SET | CIAA_ICR_SP);
    keyboard_handshake();
    return 1;
}

void kbd_interrupt_handler(void) {
    uint8 icr = ciaa_read(CIAA_ICR);
    if (!(icr & CIAA_ICR_SP)) return;
    uint8 raw = ciaa_read(CIAA_SDR);
    process_raw_keycode(raw);
    keyboard_handshake();
}

uint8 kbd_hit(void) { return buf_head != buf_tail; }

char kbd_getchar(void) {
    for (;;) {
        while (buf_head == buf_tail);
        KeyEvent ev = key_buffer[buf_tail];
        buf_tail = (buf_tail + 1) % KEY_BUF_SIZE;
        if (ev.pressed && ev.ascii != 0) return (char)ev.ascii;
    }
}
