/*
 * HID boot protocol: the parse, and nothing else.
 *
 * The six slots of the report before are the whole of the state, and
 * they are what turns a report into an event -- a key that is in this
 * report and was not in the last one went down, and a key that was in
 * the last one and is not in this one came up without anything needing
 * to be said.  NeoBench's queue takes the first of those and lets whoever
 * reads it decide what a key means; a break is not a key, and handing one
 * to the shell would be handing it something it has no use for.
 *
 * The one report that carries no answer at all is the rollover: the
 * keyboard is saying it had more keys down than six slots and cannot say
 * which.  It is dropped whole rather than read as an empty report, which
 * would release every key still held -- and it is dropped before the
 * slots are replaced, so the report after it diffs against the last one
 * that meant something.
 *
 * Nothing here touches hardware, which is the point of writing it apart
 * from any controller: AGA has no host controller to hand a report to a
 * driver (usb.c), so a table of reports on the host, driven by
 * tools/tests/test_hid.c, is the only way this code can be exercised at
 * all -- and it is the half that would run on real hardware.
 */
#include <stdint.h>
#include "hid.h"
#include "kbd.h"

/* The modifier bits read for the Shift column: the left hand and the right. */
#define HID_SHIFT       0x22u

/* Usages that mean "I could not tell you": rollover, failure, undefined. */
#define HID_ROLLOVER    0x01u
#define HID_ROLLOVER_MAX 0x03u

/* The keys of the report before, and whether the lock is on. */
static uint8_t hid_down[6];
static uint8_t hid_caps;

void nb_hid_reset(void)
{
    unsigned i;

    for (i = 0; i < 6; i++)
        hid_down[i] = 0;
    hid_caps = 0;
}

/*
 * A usage to what it produces, on the same table the keyboard on CIA-A
 * is read with (kbd.c) -- so a key gives the same answer however it
 * arrived, and there is one table of what NeoBench can type rather than
 * one per wire.  Zero is a usage with no code: a lock, a keypad operator
 * this system does not carry, a function key.  Nothing is invented here,
 * because a code handed to the shell that the shell did not ask for is
 * worse than a key that did nothing.
 */
static unsigned hid_key(unsigned usage, int shift)
{
    unsigned v;

    if (usage >= 0x04u && usage <= 0x1du)            /* a .. z          */
    {
        v = 'a' + (usage - 0x04u);
        if (shift != (int)hid_caps)  /* either one turns the letter over */
            v -= 'a' - 'A';
        return v;
    }

    if (usage >= 0x1eu && usage <= 0x27u)            /* 1 2 3 4 5 6 ...  */
    {
        static const char figs[]  = "1234567890";
        static const char marks[] = "!@#$%^&*()";

        return shift ? marks[usage - 0x1eu] : figs[usage - 0x1eu];
    }

    switch (usage)
    {
    case 0x28u: return NB_KEY_RET;
    case 0x29u: return NB_KEY_ESC;
    case 0x2au: return NB_KEY_BACK;
    case 0x2bu: return NB_KEY_TAB;
    case 0x2cu: return ' ';
    case 0x2du: return shift ? '_' : '-';
    case 0x2eu: return shift ? '+' : '=';
    case 0x2fu: return shift ? '{' : '[';
    case 0x30u: return shift ? '}' : ']';
    case 0x31u: return shift ? '|' : '\\';   /* beside Back Space        */
    case 0x32u: return shift ? '~' : '#';    /* beside Return, British   */
    case 0x33u: return shift ? ':' : ';';
    case 0x34u: return shift ? '"' : '\'';
    case 0x35u: return shift ? '~' : '`';
    case 0x36u: return shift ? '<' : ',';
    case 0x37u: return shift ? '>' : '.';
    case 0x38u: return shift ? '?' : '/';
    case 0x4fu: return NB_KEY_RIGHT;
    case 0x50u: return NB_KEY_LEFT;
    case 0x51u: return NB_KEY_DOWN;
    case 0x52u: return NB_KEY_UP;
    }
    return 0;
}

unsigned nb_hid_report(const uint8_t report[NB_HID_REPORT],
                       unsigned *out, unsigned *raw, unsigned cap)
{
    unsigned n = 0, i, j;
    int shift;

    if (!report)
        return 0;

    for (i = 2; i < NB_HID_REPORT; i++)
        if (report[i] >= HID_ROLLOVER && report[i] <= HID_ROLLOVER_MAX)
            return 0;

    shift = (report[0] & HID_SHIFT) != 0;

    for (i = 2; i < NB_HID_REPORT; i++)
    {
        unsigned u = report[i];
        unsigned k;

        if (u == 0)
            continue;

        for (j = 0; j < 6; j++)
            if (hid_down[j] == u)
                break;
        if (j < 6)
            continue;                    /* down in the last report too   */

        if (u == 0x39u)                  /* Caps Lock: a lock, not a key  */
        {
            hid_caps = (uint8_t)!hid_caps;
            continue;
        }

        k = hid_key(u, shift);
        if (!k || !out || n >= cap)
            continue;

        if (raw)
            raw[n] = u;
        out[n] = k;
        n++;
    }

    for (i = 0; i < 6; i++)
        hid_down[i] = report[2 + i];

    return n;
}
