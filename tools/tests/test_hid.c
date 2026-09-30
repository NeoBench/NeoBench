/*
 * The HID parser, driven from a table of reports on the host.
 *
 * This is the half of the USB driver that can be tested at all: AGA has
 * no PCI bus and so no host controller to hand a report to a driver
 * (usb.c), and a driver that has never been made to answer is a claim
 * rather than a driver.  So the parser is written apart from any
 * controller -- eight bytes in, the keys that went down out -- and the
 * reports below are the ones a keyboard really sends: one key, two, the
 * lock, the figures, the cursor keys, six at once, and the rollover that
 * says the keyboard ran out of slots and must not be read as "nothing is
 * held".
 *
 * The state carries from one report to the next, which is the point:
 * a key going down is a difference, not a fact, and every case below is
 * a pair -- the report that puts a key down and the report that takes it
 * away again -- because a parser that answers a report correctly and the
 * one after it wrongly would release every key the user is still holding.
 *
 *   make -C tools/tests          build and run everything
 */
#include <stdio.h>
#include <stdint.h>
#include "../../boot/rom/hid.h"
#include "../../boot/rom/kbd.h"

static unsigned fails;
static unsigned got[8];
static unsigned use[8];

#define R(a, b, c, d, e, f, g, h) \
    ((const uint8_t[]){ a, b, c, d, e, f, g, h })

/* One report, answered into the arrays the checks below read. */
static unsigned run(const uint8_t *report)
{
    unsigned i;

    for (i = 0; i < 8; i++)
    {
        got[i] = 0;
        use[i] = 0;
    }
    return nb_hid_report(report, got, use, 8);
}

static void is(const char *what, unsigned have, unsigned want)
{
    if (have != want)
    {
        fprintf(stderr, "FAIL %-22s got %u ($%x), want %u ($%x)\n",
                what, have, have, want, want);
        fails++;
    }
}

/* A key down, the count and the code it produced. */
static void held(const char *what, const uint8_t *report, unsigned code)
{
    unsigned n = run(report);

    is(what, n, 1);
    if (n)
    {
        char label[64];

        sprintf(label, "%s code", what);
        is(label, got[0], code);
    }
}

int main(void)
{
    /* ---- nothing held, and a key that goes down and comes back ------ */
    nb_hid_reset();
    is("empty report", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    held("'a' down", R(0, 0, 0x04, 0, 0, 0, 0, 0), 'a');
    is("'a' usage", use[0], 0x04);
    is("'a' held again", run(R(0, 0, 0x04, 0, 0, 0, 0, 0)), 0);
    is("'a' up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("'a' down again", R(0, 0, 0x04, 0, 0, 0, 0, 0), 'a');
    is("'a' up again", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* ---- Shift is read from the report that carries the key --------- */
    held("shift 'a'", R(0x02, 0, 0x04, 0, 0, 0, 0, 0), 'A');
    is("shift up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    held("shift '3'", R(0x02, 0, 0x20, 0, 0, 0, 0, 0), '#');
    is("shift '3' up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("'3'", R(0, 0, 0x20, 0, 0, 0, 0, 0), '3');
    is("'3' up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* A modifier on its own is not a key -- it is the next key's table. */
    is("shift alone", run(R(0x02, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("shift then 'a'", R(0x02, 0, 0x04, 0, 0, 0, 0, 0), 'A');
    is("shift then up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* ---- Caps Lock: a lock, turned once and left where it was ------- */
    is("caps press", run(R(0, 0, 0x39, 0, 0, 0, 0, 0)), 0);
    is("caps held", run(R(0, 0, 0x39, 0, 0, 0, 0, 0)), 0);
    is("caps up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("caps 'a'", R(0, 0, 0x04, 0, 0, 0, 0, 0), 'A');
    is("caps 'a' up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("caps shift 'a'", R(0x02, 0, 0x04, 0, 0, 0, 0, 0), 'a');
    is("caps shift up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    is("caps release", run(R(0, 0, 0x39, 0, 0, 0, 0, 0)), 0);
    is("caps release up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("lock off 'a'", R(0, 0, 0x04, 0, 0, 0, 0, 0), 'a');
    is("lock off up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* ---- The keys that are not characters --------------------------- */
    held("Return", R(0, 0, 0x28, 0, 0, 0, 0, 0), NB_KEY_RET);
    is("Return up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("Escape", R(0, 0, 0x29, 0, 0, 0, 0, 0), NB_KEY_ESC);
    is("Escape up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("Up", R(0, 0, 0x52, 0, 0, 0, 0, 0), NB_KEY_UP);
    is("Up up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    held("Right", R(0, 0, 0x4f, 0, 0, 0, 0, 0), NB_KEY_RIGHT);
    is("Right up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* A usage with no code produces nothing, and is still remembered --
     * so it does not come back as a key when it is pressed again. */
    is("F11 down", run(R(0, 0, 0x68, 0, 0, 0, 0, 0)), 0);
    is("F11 held", run(R(0, 0, 0x68, 0, 0, 0, 0, 0)), 0);
    held("F11 and 'a'", R(0, 0, 0x68, 0x04, 0, 0, 0, 0), 'a');
    is("F11 and 'a' up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* ---- Six keys at once ------------------------------------------- */
    {
        unsigned n = run(R(0, 0, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09));

        is("six keys", n, 6);
        if (n == 6)
        {
            is("six[0]", got[0], 'a');
            is("six[5]", got[5], 'f');
            is("six usage[5]", use[5], 0x09);
        }
        is("six up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);
    }

    /* ---- Rollover: the keyboard does not know what is down ---------- */
    is("rollover", run(R(0, 0, 0x01, 0x04, 0, 0, 0, 0)), 0);
    is("after rollover", run(R(0, 0, 0x04, 0, 0, 0, 0, 0)), 1);
    is("after rollover code", got[0], 'a');
    is("after rollover up", run(R(0, 0, 0, 0, 0, 0, 0, 0)), 0);

    /* ---- The report can be read without the usages ------------------ */
    {
        unsigned n = nb_hid_report(R(0, 0, 0x04, 0, 0, 0, 0, 0),
                                   got, 0, 8);

        is("no usages asked", n, 1);
        is("no usages code", got[0], 'a');
    }
    nb_hid_reset();

    if (fails)
    {
        fprintf(stderr, "test_hid: %u check%s failed\n", fails,
                fails == 1 ? "" : "s");
        return 1;
    }
    printf("test_hid: all checks pass\n");
    return 0;
}
