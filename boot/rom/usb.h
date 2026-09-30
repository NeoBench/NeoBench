#ifndef NB_USB_H
#define NB_USB_H

/*
 * NeoBench's USB host driver: the bus half.
 *
 * A host controller has to sit somewhere NeoBench can address it, and on
 * an AGA machine the question answers itself -- there is no PCI bus in
 * this map for one to sit on, which is also why nothing here reads an
 * address it has not been told about.  The walk below opens the same two
 * windows the ZZ9000 driver opens (zorro.h), the ones this system reads
 * without knowing what is there first, and counts what parses as a board
 * in them; that count, and the absence of a bus that could carry a
 * controller, are the whole of what this driver has to report.
 *
 * What it does not do is claim that no USB host exists anywhere: a card
 * behind a Zorro bridge would be a card a configuration ROM cannot
 * describe as one, because Zorro carries no class code, and saying "none
 * found" about hardware a probe cannot see would be the difference
 * between a driver and a rumour.  The line this builds says what the
 * window holds, and the log prints that.
 */

/*
 * Look for a controller.  Returns 0 when one is bound -- which nothing on
 * this machine can answer yet -- and otherwise a static string of the
 * words the boot log prints after "usb.device: ".  Read only, and only
 * over the autoconfig windows.
 */
const char *nb_usb_probe(void);

#endif /* NB_USB_H */
