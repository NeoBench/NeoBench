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
 * Contiguous RAM size in megabytes, chip RAM first.
 *
 * Only addresses in $200000..$BFFFFF are touched: expansion space on an
 * A1200, where a missing board simply floats the bus and the write vanishes,
 * and safely away from the custom chips at $DF0000 and the ROM at $E00000+.
 * Each step tests the last long of a megabyte (aligned, no address error)
 * and the first gap ends the contiguous run -- chip RAM and fast RAM are
 * contiguous at the bottom of the map on this machine.
 *
 * The 2 MB floor is the A1200's built-in chip RAM; there is no autoconfig
 * entry for it to read.
 */
unsigned nb_probe_mem_mb(void)
{
    static const struct
    {
        uint32_t last;              /* final long of that megabyte */
        unsigned mb;
    } step[] = {
        { 0x002ffffcUL, 3 },  { 0x003ffffcUL, 4 },  { 0x004ffffcUL, 5 },
        { 0x005ffffcUL, 6 },  { 0x006ffffcUL, 7 },  { 0x007ffffcUL, 8 },
        { 0x008ffffcUL, 9 },  { 0x009ffffcUL, 10 }, { 0x00affffcUL, 11 },
        { 0x00bffffcUL, 12 },
    };
    unsigned mb = 2;
    unsigned i;

    for (i = 0; i < sizeof step / sizeof step[0]; i++)
    {
        if (mem_at(step[i].last))
            mb = step[i].mb;
        else
            break;
    }
    return mb;
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
