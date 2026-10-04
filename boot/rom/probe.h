#ifndef NB_PROBE_H
#define NB_PROBE_H

/*
 * What the hardware probe found, and the two numbers NeoBench's startup
 * requirement is written against.
 *
 * NeoBench runs on fast RAM and reports that figure rather than the
 * machine total, because it is the machine total that is misleading: a
 * 68060 board's RAM is what the system has to work in, and the two MB
 * of chip RAM the A1200 came with are not it.  The floor is the size
 * below which there is no point starting -- a Blizzard board at full
 * 128 MB, which is what the requirement of 128+ means in hardware -- and
 * the preferred size is a full board over the 8 MB of direct fast RAM
 * that fits under the ROM window, the largest machine this memory map
 * has room for.  The boot log's tag colour, the line it prints and the
 * tick cut through the desktop's RAM gauge all read these two, so they
 * cannot drift apart.
 */
#define NB_FAST_FLOOR        128u      /* MB: below this, [FAILED]     */
#define NB_FAST_PREFERRED    136u      /* MB: below this, [ WARN ]     */

/*
 * The map is walked as far as this many megabytes -- $80000000.  Nothing
 * beyond it has been tested, so nothing beyond it may be claimed: the
 * memory probe walks up to it, and a card being given an address asks
 * the same question of the same range, so both read the one number.
 */
#define NB_MEM_TOP_MB        2048

/* Megabytes of fast RAM the probe found, chip RAM excluded. */
unsigned nb_probe_fast_mb(void);

/*
 * Does this megabyte hold memory?  The probe's own test at the probe's
 * own address -- the last long of the megabyte, two patterns, each
 * re-read -- which is what makes it safe to call for a megabyte the walk
 * already made: there is no second kind of access at stake.
 */
unsigned nb_probe_ram(uint32_t addr);

/* 1 when the address bus carries all 32 bits, 0 on a 24-bit machine. */
unsigned nb_probe_bus(void);

/*
 * The BFG9060 accelerator, and the firmware version it reports: 0..15,
 * or -1 when no signature answers at the address the card's own
 * bootrom scans ($FF040000, and only on a bus wide enough to reach it
 * rather than a truncation of it).
 *
 * -1 covers more than absence: a flash whose modules have been switched
 * off answers nothing either, and so does a machine the card cannot be
 * fitted to.  That is why the boot log prints the found case and keeps
 * quiet about the rest -- the miss is carried on the serial line, where
 * the probe's other internals go.
 */
int nb_bfg9060(void);

#endif /* NB_PROBE_H */
