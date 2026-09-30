#ifndef NB_KBD_H
#define NB_KBD_H

#include "hid.h"

/*
 * NeoBench's keyboard: three receivers, one queue.
 *
 * A key can arrive from the Amiga keyboard on CIA-A, from the serial
 * port on Paula, or from a USB keyboard when a host controller turns up
 * (hid.c, and the bus it would be on, usb.c).  Everything above this file
 * asks the queue and is not told which of them it came from -- the shell
 * has no business knowing whether the character it just read was typed on
 * the keyboard or over the wire, and the scene below has no use for the
 * difference either.
 *
 * What the queue carries is either an ASCII code, one to 127, or one of
 * the keys below, which are the ones no ASCII code tells apart.  A key
 * NeoBench has no code for is not queued at all: a table that answers
 * zero is a key that produces nothing, and dropping it here rather than
 * handing an unknown value to the shell is the whole reason the tables
 * are indexed by the hardware's own codes rather than built on the fly.
 */

#define NB_KEY_UP      0x100   /* the four cursor keys                    */
#define NB_KEY_DOWN    0x101
#define NB_KEY_LEFT    0x102
#define NB_KEY_RIGHT   0x103
#define NB_KEY_RET     0x104   /* Return: confirm what is lit             */
#define NB_KEY_ESC     0x105   /* Escape: put it back down                */
#define NB_KEY_TAB     0x106
#define NB_KEY_BACK    0x107   /* Back Space: deletes backwards          */
#define NB_KEY_LAMIGA  0x108   /* left Amiga: the start menu, as the orb  */
#define NB_KEY_RAMIGA  0x109   /* right Amiga: the same                   */

/*
 * Where the key came from, for the serial read-out that accompanies
 * every one of them.  The queue itself does not carry it: a key is a key
 * once it has been decoded, and the line below the key is the only place
 * the difference is worth a byte.
 */
#define NB_SRC_KBD     0       /* the keyboard on CIA-A                   */
#define NB_SRC_SER     1       /* Paula's receiver, at 9600 baud          */
#define NB_SRC_USB     2       /* a HID boot keyboard behind a host       */

/*
 * Arm the receivers: the keyboard's interrupt mask is taken off the ROM
 * we may have chainloaded from, because NeoBench polls the flag itself
 * and a handler underneath us would take every code before we saw it.
 * Safe to call more than once; cheap enough to call once.
 */
void nb_kbd_init(void);

/*
 * Take everything the receivers are holding into the queue.  This runs
 * from inside the wait for the next field rather than once a frame:
 * Paula's receiver carries one byte and the next arrival overwrites it,
 * so a character typed between two fields has to be taken when the beam
 * comes round rather than 20 ms after it was typed.  The keyboard's code
 * waits for the handshake before another follows it, so it is read here
 * for responsiveness rather than for safety -- both at once, since the
 * queue is what the receivers have in common.
 */
void nb_kbd_poll(void);

/* Next key, or -1 when the queue is empty. */
int  nb_kbd_get(void);

/* How many keys the queue is holding. */
int  nb_kbd_pending(void);

/*
 * A host controller's report: the keys that went down in it join the
 * queue with the HID usage as their raw code.  Nothing on AGA can call
 * this -- there is no controller to hand it a report -- and it exists so
 * that a controller has one door into the queue instead of reaching
 * under it, and so that all three sources answer the same way.  What is
 * behind it is tested: tools/tests/test_hid.c walks it on the host.
 */
void nb_kbd_usb(const uint8_t report[NB_HID_REPORT]);

/* One serial line: which receivers are armed, and how big the queue is. */
void nb_kbd_dump(void);

#endif /* NB_KBD_H */
