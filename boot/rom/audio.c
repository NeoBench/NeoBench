/*
 * Paula audio, driven directly.
 *
 * One channel plays one buffer: there is no queue and no double
 * buffering.  The buffer is not played once on its own -- Agnus reloads
 * the address and the length when the counter runs out and plays it
 * again -- so a sound that is asked to sound once is a sound somebody
 * has to stop, and stopping it is all this file does.
 *
 * The stop is timed from the field counter rather than taken from AUD0's
 * own completion request, because that request is not usable on this
 * machine: enabling a channel whose length has already been loaded puts
 * the request up before a single byte has been fetched, and once it has
 * been dropped -- with the channel stopped, the only state a drop is safe
 * in -- it is never raised again, however many times the sample plays and
 * loops.  Both halves of that are printed on serial.  What the timing
 * needs instead is room to land in, and mksound.py writes that room into
 * the sample as leading silence.
 *
 * Register offsets and bit values are from NDK hardware/custom.i and
 * hardware/dmabits.i.
 */
#include "audio.h"
#include "amiga.h"

#define CUSTOM_BASE 0x00DFF000UL
#define REG16(off)  (*(volatile uint16_t *)(CUSTOM_BASE + (off)))

/* Audio channel 0, one word of registers per channel from $a0. */
#define AUD0LCH     0x0a0               /* sample address, high          */
#define AUD0LCL     0x0a2               /* sample address, low           */
#define AUD0LEN     0x0a4               /* length in words; write arms   */
#define AUD0PER     0x0a6               /* period in colour clock ticks  */
#define AUD0VOL     0x0a8               /* 0..64, linear                 */
#define DMACON      0x096
#define DMACONR     0x002               /* read back of the same state    */
#define INTREQ      0x09c               /* write; read back at $01e      */
#define INTREQR     0x01e

#define DMAF_SETCLR 0x8000U
#define DMAF_RASTER 0x0100U
#define DMAF_MASTER 0x0200U
#define DMAF_AUD0   0x0001U

#define INTF_AUD0   0x0001U             /* channel 0 reached its end     */

/* 1 while a buffer is in flight.  File scope without an initialiser: an
 * initialised global would land in .data, which the reset build maps
 * into write-only ROM. */
static uint8_t nb_snd_state;
static uint32_t nb_snd_t0;                 /* nb_fields when it started  */
static uint32_t nb_snd_dur;                /* one pass, measured         */

static void put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void put_u(unsigned v)
{
    char buf[12];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    put(&buf[i]);
}

void nb_sound_init(void)
{
    nb_snd_state = 0;
    /*
     * Clear a completion request the previous owner may have left set.
     * The wait below no longer trusts that request -- it cannot, see the
     * note at the top of the file -- but the flag is still read twice on
     * serial, once at arm and once at stop, and both readings are worth
     * something only if it starts life clear.  A clear write only
     * affects the bits that are set in the value: bit 15 low names the
     * bits to clear, and a vertical blank request that happens to be
     * pending is simply re-raised on the next field.
     */
    REG16(INTREQ) = INTF_AUD0;
}


/*
 * Let go of the channel: mute, drop out of DMA, drop the request.  Paula
 * does not stop at the end of a buffer on its own -- the length counter
 * running out asks Agnus for the address and the length again, and the
 * buffer plays from the top for ever unless somebody disables it.  That
 * is the whole of the play-once rule -- an enable with no matching
 * disable is a loop -- and it is why the stop is timed against the pass
 * rather than left to the hardware.
 *
 * The mute is cheap insurance: the stop lands in silence -- the chime's
 * own leading silence, or the second and a half of pad the player
 * decodes after its last sample -- so the value in the latch is already
 * zero and this changes nothing a listener could hear.  It means only
 * that the channel cannot be left putting out whatever the reload
 * manages to fetch in the field between the pass ending and this write.
 * The request state is read before the clear so the line says whether
 * the hardware ever raised it.
 *
 * `why` is the line's own tail: " poll" when the main loop noticed,
 * " stop" when something asked for it, and nothing when the boot waited
 * the pass out in front of the desktop.
 */
static void release(const char *why)
{
    unsigned req = (REG16(INTREQR) & INTF_AUD0) ? 1u : 0u;

    REG16(AUD0VOL) = 0;
    REG16(DMACON) = DMAF_AUD0;                 /* bit 15 low = off     */
    REG16(INTREQ) = INTF_AUD0;                 /* and the request      */
    nb_snd_state = 0;

    put(">sound done fields=");
    put_u((unsigned)(nb_fields - nb_snd_t0));
    put(" dur=");
    put_u((unsigned)nb_snd_dur);
    put(" req=");
    put_u(req);
    put(why);
    put("\n\r");
}

/*
 * Everything the two callers share: take the channel back from whoever
 * held it, load the four registers with the channel off, print the run
 * on serial while DMA is still down -- the line costs a couple of
 * milliseconds and those have to come off the boot rather than off the
 * sample -- then enable and start the clock the stop is read against.
 */
static unsigned arm(unsigned addr, unsigned dlen, unsigned period,
                    unsigned rate, unsigned vol)
{
    unsigned dbg_pre;

    /*
     * Stop the channel before touching it.  The ROM owns Paula when
     * NeoBench takes over and may still be playing its own startup
     * sound: an enabled channel that runs to the end of the ROM's
     * buffer raises AUD0 again microseconds after it is cleared, and
     * the completion test then passes before ours has played a byte.
     *
     * The two writes that follow happen with the channel off, which is
     * the only state the request can safely be dropped in: clearing
     * AUD0 out of a channel that is loaded and running takes the
     * channel down with it, and the wait further on then runs to its
     * bound instead of ending.
     */
    REG16(DMACON) = DMAF_AUD0;                    /* bit 15 low = off    */
    REG16(INTREQ)  = INTF_AUD0;                   /* arm against a stale */
    dbg_pre = REG16(INTREQR);                     /* still set after that */

    REG16(AUD0VOL) = (uint16_t)vol;
    REG16(AUD0PER) = (uint16_t)period;
    REG16(AUD0LCH) = (uint16_t)((unsigned)addr >> 16);
    REG16(AUD0LCL) = (uint16_t)((unsigned)addr & 0xffffU);

    /*
     * The length is loaded with the channel stopped -- it has to be,
     * because dropping the request out of a channel that is enabled
     * silences that request for good, and the wait further on then runs
     * to its bound while the sample plays and loops anyway.  Whatever
     * the load itself puts up is dropped here too, still with DMA off,
     * and the enable is the very last thing done: from here the only
     * event that can set AUD0 is this sample running out.
     */
    REG16(AUD0LEN) = (uint16_t)(dlen >> 1);

    put(">sound start period=");
    put_u(period);
    put(" rate=");
    put_u(rate);
    put(" bytes=");
    put_u(dlen);
    put(" vol=");
    put_u(vol);
    put(" intreq=");
    put_u(dbg_pre);
    put("\n\r");

    REG16(INTREQ) = INTF_AUD0;

    /*
     * Channel 0 is joined to the master and raster enables that are
     * already holding the display up.  Every other DMA channel is idle,
     * so this write is correct whichever way DMACON's set/clear bit is
     * read: the three bits named are the three that must end up set.
     */
    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_AUD0;

    /*
     * One pass of the sample, in fields.
     *
     * Paula takes one byte every period colour clocks, a colour clock is
     * 3 546 895 Hz on this machine, and a field is a fiftieth of a
     * second -- so the pass is dlen * period colour clocks and a field is
     * 70 937.9 of them.  The division is done by 2 216 rather than by
     * 70 938 so that it divides a constant (the link carries no libgcc):
     * 70 938 / 32 is 2 216.8, and taking the smaller of the two, after
     * rounding the byte count up by 31 before the shift, leaves the
     * answer on the high side of the truth.  Stopping low cuts the chime
     * off; stopping high only waits out a field that sounds of nothing,
     * because the sample is silent in front of itself for the fifteen
     * hundredths mksound.py put there.  The two extra fields cover the
     * field the counter is actually read in.
     *
     * The player leans on the same slack the other way: it decodes a
     * second and a half of silence after its last sample, so a stop that
     * comes late -- a present runs for up to three quarters of a second
     * and the main loop does not poll through one -- lands in that pad
     * rather than in the head of the buffer Paula has gone back to.
     */
    nb_snd_dur = (((dlen + 31u) >> 5) * period) / 2216u + 2u;
    nb_snd_t0 = nb_fields;
    nb_snd_state = 1;
    return period;
}

unsigned nb_sound_start(unsigned addr, unsigned bytes,
                        unsigned rate, unsigned vol)
{
    unsigned period;

    if (rate == 0u)
        return 0;
    if (addr < NB_SND_BASE || addr > NB_SND_BASE + NB_SND_MAX)
        return 0;
    if (bytes < 2u || (bytes & 1u) ||
        bytes > NB_SND_BASE + NB_SND_MAX - addr)
        return 0;

    /* A colour clock over the rate, floored where the hardware is. */
    period = 3546895u / rate;
    if (period < 124u)
        period = 124u;
    if (period > 65535u)
        period = 65535u;
    if (vol > 64u)
        vol = 64u;

    return arm(addr, bytes, period, rate, vol);
}

void nb_sound_stop(void)
{
    if (nb_snd_state)
        release(" stop");
}

int nb_sound_busy(void)
{
    return nb_snd_state != 0u;
}

unsigned nb_sound_play(const void *src, unsigned bytes, unsigned vol)
{
    const unsigned char *p = (const unsigned char *)src;
    const unsigned hdr = 16u;
    unsigned period, dlen, rate, i;

    if (!p || bytes < hdr || bytes > NB_SND_MAX + hdr)
        return 0;
    if (p[0] != 'N' || p[1] != 'S' || p[2] != 'N' || p[3] != 'D')
        return 0;

    period = ((unsigned)p[4] << 8) | (unsigned)p[5];
    dlen   = ((unsigned)p[8] << 24) | ((unsigned)p[9] << 16) |
             ((unsigned)p[10] << 8) | (unsigned)p[11];
    rate   = ((unsigned)p[12] << 24) | ((unsigned)p[13] << 16) |
             ((unsigned)p[14] << 8) | (unsigned)p[15];

    /* 124 is the lowest period the audio device documents; the OS would
     * refuse anything below it, and there is no quality to gain. */
    if (period < 124u || period > 65535u || rate == 0u)
        return 0;
    if (dlen < 2u || (dlen & 1u) || dlen > bytes - hdr || dlen > NB_SND_MAX - hdr)
        return 0;
    if (vol > 64u)
        vol = 64u;

    for (i = 0; i < dlen; i++)                    /* samples -> chip RAM */
        *(volatile uint8_t *)(NB_SND_BASE + i) = p[hdr + i];

    /*
     * The header is deliberately left behind.  Paula fetches from the
     * address in AUD0LCH/LCL and plays whatever bytes it finds there,
     * so a copy that started at p[0] would put sixteen bytes of "NSND"
     * -- the magic, the period, the length -- into the first two
     * milliseconds of the sound.  That is a click at the start, and
     * because Agnus reloads the same address at the end of every pass,
     * the same click at the end of each one after the first.
     */

    arm((int)NB_SND_BASE, dlen, period, rate, vol);

    /*
     * Wait here rather than in the main loop: the desktop is drawn
     * between the two, and the time it takes to draw would otherwise be
     * counted as playback time.
     */
    while (nb_fields - nb_snd_t0 < nb_snd_dur)
        ;

    release("");                  /* one pass, then take it back        */
    return rate;
}

void nb_sound_poll(void)
{
    if (nb_snd_state == 1u && nb_fields - nb_snd_t0 >= nb_snd_dur)
        release(" poll");
}
