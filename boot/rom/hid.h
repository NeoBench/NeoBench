#ifndef NB_HID_H
#define NB_HID_H

#include <stdint.h>

/*
 * HID boot protocol: everything a keyboard has to say, in eight bytes.
 *
 * The boot protocol is what a keyboard falls back to when nobody has
 * spoken HID to it yet, and it is deliberately small: a byte of modifier
 * bits, a reserved byte, and six slots holding the usages of the keys
 * that are down.  Every keyboard can be asked for it, it arrives without
 * an enumeration to speak of, and it carries a whole keyboard's worth of
 * state -- which is why a driver that can do this much and nothing else
 * can still read every key.
 *
 * It is written here, apart from any controller, for one reason: this is
 * the half of the USB path that can be tested.  AGA has no PCI bus and so
 * no host controller to hand a report to it (usb.c), and the boot log
 * says so in as many words -- so the parser is the part that has to earn
 * its place by being driven from a table of reports on the host, where
 * tools/tests/test_hid.c walks every key in it.
 */

/* One report: modifiers, a byte nobody uses, six keys. */
#define NB_HID_REPORT   8u

/*
 * One report against the one before it.  The keys that went *down* are
 * written to `out' as the codes NeoBench's queue carries (kbd.h), with
 * the HID usage they came from written beside them to `raw' -- which may
 * be null when the caller has no use for it.  At most `cap' of them; the
 * count comes back.
 *
 * A key that is still held is not a key that went down, so the report
 * before is what makes the answer: a break is read as the absence of a
 * key in the next report, which is what the six slots are for.  Nothing
 * here touches hardware, so the same call on the host answers exactly
 * what it answers in the machine.
 */
unsigned nb_hid_report(const uint8_t report[NB_HID_REPORT],
                       unsigned *out, unsigned *raw, unsigned cap);

/* Forget the report before: between tests, or across a reconnect. */
void nb_hid_reset(void);

#endif /* NB_HID_H */
