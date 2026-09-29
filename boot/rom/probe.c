/*
 * Hardware probes for the boot self-test.
 *
 * The instruction probes (FPU, MMU, MOVE16, MOVEC PCR and with them the
 * CPU model) live in fline.S; this file holds their shared frame addresses
 * plus the memory size and display probes.
 */
#include <stdint.h>
#include "amiga.h"

/*
 * Frame fields nb_fline_handler matches against.  Written immediately
 * before each probe instruction, so the handler can recognise its own
 * fault inside the exception frame (see fline.S).
 */
uint32_t nb_probe_pc;               /* address of the instruction under test */
uint32_t nb_probe_recover;          /* ...and where control returns on a trap */

/* Instruction probes in fline.S. */
extern int nb_probe_move16(void);
extern int nb_probe_pcr(void);
extern unsigned nb_probe_mmu(void);

/*
 * CPU model, as one of 20/30/40/60 for a 68020 through 68060.
 *
 * NeoBench targets the 68060 only, so this answers "is this machine
 * eligible?" for the boot log.  Two instructions pick the generation:
 *
 *   MOVE16 (F600 + addr.l) -- runs on the 68040/68060, line-F below;
 *   MOVEC PCR (4E7A 0808)  -- the PCR is a 68060-only control register:
 *                             it executes on the 68060 and is an illegal
 *                             instruction on the 68040, on silicon as
 *                             well as in the emulator.
 *
 * That splits every CPU from 68020 up with no CPU ID register to read.
 * The MMU probe settles 68020 vs 68030 when MOVE16 is absent.
 */
int nb_probe_cpu(void)
{
    if (!nb_probe_move16())
        return nb_probe_mmu() ? 30 : 20;
    return nb_probe_pcr() ? 60 : 40;
}

static int mem_at(uint32_t addr)
{
    volatile uint32_t *p = (volatile uint32_t *)addr;

    /* Two distinct patterns: a single pass can succeed on floating bus
     * data that happens to repeat what was last written. */
    *p = 0x55aa33ccUL;
    if (*p != 0x55aa33ccUL)
        return 0;
    *p = 0xaa55cc33UL;
    if (*p != 0xaa55cc33UL)
        return 0;
    return 1;
}

/*
 * Read one long from an absolute machine address.
 *
 * The address arrives as data, not as a pointer expression: building
 * $00000000 as (T *)0 and then offsetting it would form a pointer into a
 * zero-length object, which is undefined -- the compiler is entitled to
 * turn the whole rest of the caller into a trap, and at -Os it does
 * exactly that, which leaves the guest executing TRAP #7 with no vector
 * installed.  Keeping the function opaque is what stops it proving the
 * argument folds back to 0 once it is inlined.
 */
static __attribute__((noinline)) uint32_t peek(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/*
 * Does bit 24 of an address reach the bus?
 *
 * On a 24-bit bus $01000000 and $000000 are the same cell -- the high
 * address lines are simply not wired -- so the reset stack pointer and
 * the reset PC come back out of both.  On a 32-bit bus they do not: what
 * sits at $01000000 is whatever the accelerator put there.  The test
 * reads only, because writing would be destructive on exactly the
 * machine it is meant to protect, and it takes a pair rather than one
 * sample so that RAM which happens to hold one plausible long cannot
 * pass for a mirror.
 *
 * This is the gate on everything above 16 MB.  A write there wraps into
 * chip RAM on a 24-bit machine, and chip RAM is where the frame buffer,
 * the heap and the stack all live; every address at or above 16 MB is
 * truncated the same way, so one sample settles all of them.
 */
static int bus32(void)
{
    if (peek(0x01000000UL) != peek(0x00000000UL))
        return 1;
    if (peek(0x01000004UL) != peek(0x00000004UL))
        return 1;
    return 0;
}

/*
 * Where the RAM is, in megabytes, as two runs: everything below the
 * chipset window, and everything from 17 MB up.
 *
 * The low run only touches $200000..$CFFFFF: expansion space on an
 * A1200, where a missing board simply floats the bus and the write
 * vanishes, and safely away from the custom chips at $DF0000 and the ROM
 * at $E00000+.  Each step tests the last long of a megabyte (aligned,
 * no address error) and the first gap ends that run.
 *
 * $D80000..$FFFFFF is never tested at all: the chipset and the ROM are
 * mapped there and a write to $DFFFFC is a write to a custom-chip
 * register.  Megabytes 14 to 16 are therefore counted only when the low
 * run reached 13, which is the evidence that the map continues under the
 * window -- and the whole high run is skipped on a 24-bit bus, where it
 * would land back in chip RAM (bus32).
 *
 * The 2 MB floor is the A1200's built-in chip RAM; there is no autoconfig
 * entry for it to read.
 */
/* DIAGNOSTIC: what the probe saw, printed by the boot log. */
unsigned nb_probe_dbg_low;
unsigned nb_probe_dbg_top;
unsigned nb_probe_dbg_bus;
unsigned nb_probe_dbg_first;
unsigned nb_probe_dbg_count;

#define NB_MEM_TOP_MB   4096              /* DIAGNOSTIC: full scan */

static void mem_runs(unsigned *low, unsigned *top)
{
    unsigned n;

    *low = 2;
    *top = 0;

    for (n = 3; n <= 13; n++)
    {
        if (!mem_at(n * 0x100000UL - 4UL))
            break;
        *low = n;
    }

    if (!bus32())
        return;

    for (n = 17; n < NB_MEM_TOP_MB; n++)
    {
        if (!mem_at(n * 0x100000UL - 4UL))
            continue;                       /* DIAGNOSTIC: do not stop */
        if (!nb_probe_dbg_first)
            nb_probe_dbg_first = n;
        nb_probe_dbg_count++;
        *top = n;
    }
}

/*
 * Fast RAM: everything above the A1200's built-in 2 MB of chip RAM,
 * summed from both runs so a hole in the map is never mistaken for
 * memory.  This is the figure NeoBench's startup requirement is written
 * against -- 50 MB is the floor it will not run below, 80 MB is what the
 * system is built for -- so the boot log and the desktop report it
 * rather than the machine total.
 *
 * A card can put its RAM anywhere: the low run finds the Zorro-style
 * boards at $200000 and the high run finds accelerator RAM, which on a
 * 68060 A1200 board starts above 16 MB and never appears below it.
 */
/* DIAGNOSTIC: what the probe saw (globals declared above mem_runs). */

unsigned nb_probe_fast_mb(void)
{
    unsigned low, top, fast;

    mem_runs(&low, &top);
    nb_probe_dbg_low = low;
    nb_probe_dbg_top = top;
    nb_probe_dbg_bus = bus32();

    fast = low > 2 ? low - 2 : 0;
    if (top)
        fast += top - 16 + (low >= 13 ? 3 : 0);
    return fast;
}

/*
 * Display present: the engine was programmed by amiga_display_init() and
 * the frame buffer still answers like RAM.  The mode registers are
 * write-only, so this is the honest test -- the path that actually puts
 * pixels on the screen works.
 *
 * Two pixels of plane 0 are tested and put back, so the probe leaves the
 * console untouched.
 */
int nb_probe_rtg(void)
{
    volatile uint8_t *fb = (volatile uint8_t *)NB_FB_BASE;
    const uint8_t save0 = fb[0];
    const uint8_t save1 = fb[1];
    int ok = amiga_display_ready();

    fb[0] = 0x5a;
    fb[1] = 0xa5;
    if (fb[0] != 0x5a || fb[1] != 0xa5)
        ok = 0;
    fb[0] = save0;
    fb[1] = save1;
    return ok;
}
