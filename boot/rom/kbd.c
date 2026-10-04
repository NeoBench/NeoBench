/*
 * NeoBench's keyboard: three receivers, one queue.
 *
 * A key arrives from one of three places and nothing above this file is
 * told which: the Amiga keyboard hanging off CIA-A's serial port, Paula's
 * receiver at 9600 baud, and a HID report from a host controller when one
 * exists (hid.c) -- which on AGA it does not, as usb.c says on the screen.
 * What joins the three is that each knows its own code for a key and the
 * queue does not: the raw key position from the keyboard, the byte from
 * the wire, the usage from the report.  The tables below turn the first
 * into what the queue carries, and the other two are turned by functions
 * beside them.
 *
 * The keyboard is the part worth explaining, because nearly every step of
 * it is the opposite of what the name suggests.
 *
 * It sends one byte at a time and holds it until the machine has said it
 * has that one, then waits to be told the next may follow.  The byte is
 * the key's position in the keyboard's own numbering shifted up one bit
 * with the break flag in the space below it, all of it inverted -- so a
 * make of `a' ($20 << 1 = $40) arrives as $BF and the same key coming up
 * arrives as $41.  Nothing of that is negotiable, and the ROM's own key
 * table is where the positions come from.
 *
 * The flag that says a byte is waiting lives on CIA-A's interrupt
 * register, and reading that register is not a question but a confession:
 * it clears every flag the chip holds on the way out.  NeoBench polls the
 * keyboard itself and owns the machine by the time it runs, so nothing is
 * waiting on those flags -- and the flag is a level rather than an edge,
 * so a byte cannot slip past between two polls.
 *
 * The acknowledgement is the gesture the ROM makes and for the same
 * reason: no second code follows until it does.  Drive the keyboard's
 * data line low by turning the serial port into an output and loading it
 * with a zero, hold it, let it go -- the edge back up is what the keyboard
 * reads as "send the next one".  A driver that forgets it has a keyboard
 * that stops after one key; one that pulses too briefly has one that
 * loses the second.
 */
#include <stdint.h>
#include "kbd.h"
#include "amiga.h"

/*
 * CIA-A answers at $BFE001 with one register per $100 on the odd byte
 * lane: C is the keyboard's shift register, D the interrupt register, E
 * the control register -- which puts them at $BFEC01, $BFED01, $BFEE01.
 */
#define CIAA_SDR        0x00bfec01UL
#define CIAA_ICR        0x00bfed01UL
#define CIAA_CRA        0x00bfee01UL

#define ICR_SP          0x08u       /* serial port: a byte is waiting      */
#define CRA_SPMODE      0x40u       /* the serial port drives the line     */

/* The keys that change what the other keys produce, in the keyboard's
 * own numbering -- the ROM's key table, raw $60 upwards. */
#define RAW_LSHIFT      0x60u
#define RAW_RSHIFT      0x61u
#define RAW_CAPSLOCK    0x62u
#define RAW_CTRL        0x63u
#define RAW_LALT        0x64u
#define RAW_RALT        0x65u

/* the KMOD_ bits and their values are kbd.h's, where anything that reads
 * them rather than setting them can find them too */

/*
 * How long the acknowledgement is held, in empty loops.  Chosen, not
 * measured: the keyboard is clocked at a few megahertz, this is well
 * over a microsecond on any processor NeoBench boots on and a small
 * fraction of a field on all of them, and nothing here has been watched
 * on hardware.  What it must not be is long enough to be mistaken for a
 * byte on its way back, which at these lengths it is not.
 */
#define KBD_PULSE       4000u

#define NB_KQ           64u         /* power of two: the ring masks        */

static uint16_t kq[NB_KQ];
static unsigned kq_head;
static unsigned kq_tail;
static unsigned kmods;

/* ------------------------------------------------------------------ *
 * The serial line this driver writes its own read-out on
 * ------------------------------------------------------------------ */

static void s_put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void s_u(unsigned v)
{
    char b[11];
    int i = 11;

    b[--i] = '\0';
    do
    {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    s_put(&b[i]);
}

/* Exactly `n' hexadecimal digits, most significant first. */
static void s_hex(unsigned v, unsigned n)
{
    static const char dig[] = "0123456789abcdef";

    while (n--)
    {
        amiga_serial_putc(dig[(v >> (n * 4u)) & 0x0fu]);
    }
}

static void s_nl(void)
{
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}

static const char *src_name(unsigned src)
{
    switch (src)
    {
    case NB_SRC_KBD: return "kbd";
    case NB_SRC_SER: return "ser";
    }
    return "usb";
}

static const char *key_name(int key)
{
    switch (key)
    {
    case NB_KEY_UP:     return "up";
    case NB_KEY_DOWN:   return "down";
    case NB_KEY_LEFT:   return "left";
    case NB_KEY_RIGHT:  return "right";
    case NB_KEY_RET:    return "ret";
    case NB_KEY_ESC:    return "esc";
    case NB_KEY_TAB:    return "tab";
    case NB_KEY_BACK:   return "back";
    case NB_KEY_LAMIGA: return "lamiga";
    case NB_KEY_RAMIGA: return "ramiga";
    }
    return "?";
}

/*
 * One line per key that arrives, in whichever receiver it arrived in:
 * where it came from, what the hardware said, and what it became.  The
 * ones with no code are read out too, because "the key did nothing" and
 * "the key never arrived" are the same line apart from the word -- and it
 * is the raw code that says whether the table is wrong or the key was
 * never sent.
 */
static void key_line(unsigned src, unsigned raw, int key)
{
    s_put(">key src=");
    s_put(src_name(src));
    s_put(" raw=$");
    s_hex(raw, 2);
    s_put(" got=");

    if (key <= 0)
    {
        s_put("none");
    }
    else
    {
        s_hex((unsigned)key, 4);
        if (key >= 0x20 && key < 0x7f)
        {
            s_put(" ch=");
            amiga_serial_putc((char)key);
        }
        else
        {
            s_put(" name=");
            s_put(key_name(key));
        }
    }
    s_nl();
}

/*
 * One key from one source: read out, then queued when there is a code for
 * it.  A queue with room for 64 and the main loop draining it every field
 * does not overflow without something being wrong, so the one case that
 * cannot be taken silently is said on the spot rather than counted for a
 * dump that never happens.
 */
static void push(unsigned src, unsigned raw, int key)
{
    unsigned next = (kq_head + 1u) & (NB_KQ - 1u);
    int queued = 0;

    if (key > 0 && next != kq_tail)
    {
        kq[kq_head] = (uint16_t)key;
        kq_head = next;
        queued = 1;
    }

    key_line(src, raw, key);

    if (key > 0 && !queued)
    {
        s_put(">key queue full, key dropped");
        s_nl();
    }
}

/* ------------------------------------------------------------------ *
 * What each key produces
 * ------------------------------------------------------------------ */

/*
 * Raw position to character, without and with Shift -- the two tables a
 * keymap keeps, indexed by the raw position alone so that the state is
 * read from the modifiers rather than from the key.  A key that answers
 * zero produces nothing at all, which is what keeps the F keys, the lock
 * and every position nothing is defined at out of the shell's way: a code
 * this table does not name is a code the shell must never see.
 *
 * The figures are the ROM's USA0 defaults, the table the machine itself
 * documents, with the two keys a British keyboard has beside Return and
 * by Left Shift filled in (raw $2B and $30) -- so `#', `~' and the
 * backslash are all on their British keys as well as on the American
 * ones.  The pound sign and the not-sign are not ASCII and are left out
 * until keymaps come with Preferences: every character below can be typed
 * on this table, and none of them is one NeoBench would have to invent.
 */
static const uint16_t k_plain[128] = {
    /* 00 */ '`', '1', '2', '3', '4', '5', '6', '7',
    /* 08 */ '8', '9', '0', '-', '=', '\\', 0, '0',
    /* 10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 18 */ 'o', 'p', '[', ']', 0, '1', '2', '3',
    /* 20 */ 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k',
    /* 28 */ 'l', ';', '\'', '#', 0, '4', '5', '6',
    /* 30 */ '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
    /* 38 */ ',', '.', '/', 0, '.', '7', '8', '9',
    /* 40 */ ' ', NB_KEY_BACK, NB_KEY_TAB, NB_KEY_RET, NB_KEY_RET,
             NB_KEY_ESC, 0, 0,
    /* 48 */ 0, 0, '-', 0, NB_KEY_UP, NB_KEY_DOWN, NB_KEY_RIGHT,
             NB_KEY_LEFT,
    /* 50 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 58 */ 0, 0, 0, 0, '/', '*', '+', 0,
    /* 60 */ 0, 0, 0, 0, 0, 0, NB_KEY_LAMIGA, NB_KEY_RAMIGA,
    /* 68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 70 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 78 */ 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t k_shift[128] = {
    /* 00 */ '~', '!', '@', '#', '$', '%', '^', '&',
    /* 08 */ '*', '(', ')', '_', '+', '|', 0, '0',
    /* 10 */ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    /* 18 */ 'O', 'P', '{', '}', 0, '1', '2', '3',
    /* 20 */ 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K',
    /* 28 */ 'L', ':', '"', '~', 0, '4', '5', '6',
    /* 30 */ '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M',
    /* 38 */ '<', '>', '?', 0, '.', '7', '8', '9',
    /* 40 */ ' ', NB_KEY_BACK, NB_KEY_TAB, NB_KEY_RET, NB_KEY_RET,
             NB_KEY_ESC, 0, 0,
    /* 48 */ 0, 0, '-', 0, NB_KEY_UP, NB_KEY_DOWN, NB_KEY_RIGHT,
             NB_KEY_LEFT,
    /* 50 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 58 */ 0, 0, 0, 0, '/', '*', '+', 0,
    /* 60 */ 0, 0, 0, 0, 0, 0, NB_KEY_LAMIGA, NB_KEY_RAMIGA,
    /* 68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 70 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 78 */ 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * A key that changes the table rather than entering it.  Returns 1 when
 * the position was one, which is what keeps it out of the queue: a Shift
 * is not a key anybody reads, and the state it leaves behind is what the
 * next key is translated with.  The lock is a lock -- it turns over on the
 * way down and stays as set on the way up.
 */
static int mod_down(unsigned raw)
{
    switch (raw)
    {
    case RAW_LSHIFT:
    case RAW_RSHIFT:   kmods |= KMOD_SHIFT; return 1;
    case RAW_CAPSLOCK: kmods ^= KMOD_CAPS;  return 1;
    case RAW_CTRL:     kmods |= KMOD_CTRL;  return 1;
    case RAW_LALT:
    case RAW_RALT:     kmods |= KMOD_ALT;   return 1;
    }
    return 0;
}

static void mod_up(unsigned raw)
{
    switch (raw)
    {
    case RAW_LSHIFT:
    case RAW_RSHIFT:   kmods &= ~KMOD_SHIFT; break;
    case RAW_CAPSLOCK: break;                /* a lock: leave it set      */
    case RAW_CTRL:     kmods &= ~KMOD_CTRL;  break;
    case RAW_LALT:
    case RAW_RALT:     kmods &= ~KMOD_ALT;   break;
    }
}

/*
 * Raw position to what the queue carries, with what is held down applied.
 * The lock is a typewriter's lock and nothing more: it turns over the
 * letters and leaves the figures and the punctuation where they are, and
 * holding Shift with it puts the letters back -- which is what reading the
 * column first and then deciding gives for free.
 */
static int translate(unsigned raw)
{
    int shifted = (kmods & KMOD_SHIFT) != 0;

    if ((kmods & KMOD_CAPS) && k_plain[raw] >= 'a' && k_plain[raw] <= 'z')
        shifted = !shifted;

    return (int)(shifted ? k_shift[raw] : k_plain[raw]);
}

/*
 * The handshake: the two edges the keyboard is waiting for.
 *
 * Loading the shift register with a zero and turning the port into an
 * output is what puts a zero on the keyboard's data line -- the port
 * presents the byte it holds -- and letting go of the bit again is what
 * lets the keyboard know the machine has it.  Between the two is a
 * bounded spin, because there is no timer running here that this code is
 * allowed to wait on and because the length only has to be longer than a
 * keyboard clock and shorter than anything else the machine does.
 */
static void kbd_ack(void)
{
    volatile uint8_t *cra = (volatile uint8_t *)CIAA_CRA;
    uint8_t cr = *cra;
    volatile unsigned spin = KBD_PULSE;

    *(volatile uint8_t *)CIAA_SDR = 0x00;
    *cra = (uint8_t)(cr | CRA_SPMODE);
    while (spin--)
        ;
    *cra = (uint8_t)(cr & (uint8_t)~CRA_SPMODE);
}

/* ------------------------------------------------------------------ *
 * The three receivers
 * ------------------------------------------------------------------ */

/*
 * Everything the keyboard is holding.
 *
 * The interrupt register is read first and it cannot be read twice: it
 * clears every flag the chip has on the way out, so what was there a
 * moment ago is not there after.  That is written down as a cost of
 * taking the machine in nb_kbd_init(), not hidden here -- here the flag
 * is simply the answer to "is there a byte", and it is a level, so a
 * code waiting for this poll is still waiting when it arrives.
 */
static void kbd_rx(void)
{
    unsigned wire, raw;

    if (!(*(volatile uint8_t *)CIAA_ICR & ICR_SP))
        return;

    /*
     * The byte inverted, then split: bit 0 is the break flag and the
     * seven above it the key's position, both sent upside down because
     * that is how the keyboard presents them.  The codes with no position
     * of their own -- the power-up stream and the keyboard's own
     * complaints -- decode to positions the tables do not name and are
     * acknowledged like any other, because the keyboard is waiting for
     * the handshake either way and a keyboard left waiting is a keyboard
     * that stops.
     */
    wire = ~*(volatile uint8_t *)CIAA_SDR & 0xffu;
    raw = (wire >> 1) & 0x7fu;

    if (wire & 1u)
    {
        mod_up(raw);                /* a break: the key came up           */
    }
    else if (!mod_down(raw))
    {
        push(NB_SRC_KBD, raw, translate(raw));
    }

    kbd_ack();
}

/*
 * A byte from the wire to the code it stands for.
 *
 * The five that have a code of their own are the ones no shell can do
 * without -- Return, Tab, Escape and the two keys that delete -- and the
 * only ones where an ASCII control code and a key are the same thing.
 * Everything printable is itself; every other control code produces
 * nothing, because handing the shell a value it would have to guess the
 * meaning of is worse than a key that did nothing.
 */
static int wire_key(unsigned c)
{
    switch (c)
    {
    case '\r':
    case '\n':  return NB_KEY_RET;
    case '\t':  return NB_KEY_TAB;
    case 0x1b:  return NB_KEY_ESC;
    case 0x08:
    case 0x7f:  return NB_KEY_BACK;
    }

    if (c >= 0x20u && c < 0x7fu)
        return (int)c;
    return 0;
}

/*
 * Paula, drained a byte at a time.
 *
 * The receiver holds one byte and the next arrival overwrites it, which
 * is why this runs from inside the wait for the next field rather than
 * once a frame: a character typed between two fields has to be taken when
 * the beam comes round, not 20 ms later.  The eighth bit is dropped --
 * the queue carries ASCII and the keys above it, and a byte with the top
 * bit set is either somebody else's character set or a line error, and
 * neither becomes a key by arriving on this port.
 */
static void ser_rx(void)
{
    int b;

    while ((b = amiga_serial_poll()) >= 0)
    {
        if (b & 0x80)
            continue;
        push(NB_SRC_SER, (unsigned)b, wire_key((unsigned)b));
    }
}

/* ------------------------------------------------------------------ *
 * The queue, and the outside of the driver
 * ------------------------------------------------------------------ */

void nb_kbd_init(void)
{
    volatile uint8_t *icr = (volatile uint8_t *)CIAA_ICR;
    volatile uint8_t *cra = (volatile uint8_t *)CIAA_CRA;

    /*
     * Take the keyboard.
     *
     * Two writes.  The first clears the serial port's bit in CIA-A's
     * interrupt mask -- clearing rather than setting, because that
     * register is a set/clear register: bit 7 low says "these bits", and
     * writing only the keyboard's bit leaves the timers and the alarm
     * exactly where they were.  What it takes away is the ability of the
     * ROM underneath to see a key, which is the whole point.  Reading the
     * flags themselves is a separate cost and is paid when a key is read
     * (kbd_rx): NeoBench owns the machine by then, the vertical blank is
     * already its own, and the timers this system does not start are not
     * waiting to be told the time.
     *
     * The second puts the port back into input mode, which is where the
     * keyboard expects to find it between handshakes -- a port left driven
     * from the last acknowledgement holds the keyboard's data line down,
     * and a keyboard with its data line held down sends nothing at all.
     */
    *icr = ICR_SP;
    *cra = (uint8_t)(*cra & (uint8_t)~CRA_SPMODE);
}

void nb_kbd_poll(void)
{
    kbd_rx();
    ser_rx();
}

int nb_kbd_get(void)
{
    int v;

    if (kq_head == kq_tail)
        return -1;
    v = (int)kq[kq_tail];
    kq_tail = (kq_tail + 1u) & (NB_KQ - 1u);
    return v;
}

int nb_kbd_pending(void)
{
    return (int)((kq_head - kq_tail) & (NB_KQ - 1u));
}

/*
 * What is held down at this moment, which is only ever the answer for as
 * long as it still is: the keys are read on the way down and on the way
 * up, and a chord is read while its own key is being pressed, which is
 * the one moment the answer has to be right.  The shell, which is the
 * other consumer of the queue, never asks -- a character it has been
 * given has already had its modifiers applied.
 */
unsigned nb_kbd_mods(void)
{
    return kmods;
}

/*
 * What a HID report says is held down, read out of the boot keyboard's
 * own bitmap: bits 0 to 3 are the left-hand control, shift, alt and
 * left-Amiga, bits 4 to 7 the right-hand ones of the same four.  A
 * report carries this as a state rather than as keys that went down and
 * came up, so it is taken on every report and stands until the next;
 * the lock is left where it is, since the keyboard on CIA-A is the one
 * that has a light for it.
 */
static unsigned hid_mods(unsigned m)
{
    unsigned v = 0;

    if (m & 0x11u) v |= KMOD_CTRL;
    if (m & 0x22u) v |= KMOD_SHIFT;
    if (m & 0x44u) v |= KMOD_ALT;
    return v;
}

/*
 * A host controller's report, as it would arrive: the keys that went down
 * in it join the queue with the usage as their raw code, which is what
 * keeps the line below a key the same shape as one from CIA-A.
 *
 * Nothing on AGA can call this -- there is no controller to hand it a
 * report (usb.c) -- and it exists so that a controller has one place to
 * push into rather than reaching under the queue, and so that all three
 * sources have the same door.  What is behind it is the part that can be
 * tested, and tools/tests/test_hid.c tests it on the host.
 */
void nb_kbd_usb(const uint8_t report[NB_HID_REPORT])
{
    unsigned keys[6], usages[6];
    unsigned n, i;

    kmods = (kmods & KMOD_CAPS) | hid_mods(report[0]);
    n = nb_hid_report(report, keys, usages, 6u);
    for (i = 0; i < n; i++)
        push(NB_SRC_USB, usages[i], (int)keys[i]);
}

void nb_kbd_dump(void)
{
    s_put(">kbd cia-a=$bfec01 ack=pulse queue=");
    s_u(NB_KQ);
    s_put(" recv=kbd,ser");
    s_nl();
}
