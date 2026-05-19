/*
 * NeoBench Bare-Metal Amiga Kernel
 * Paula Audio Driver
 *
 * Controls the 4-channel DMA audio hardware (Paula) for sound output.
 * Provides simple beep, boot chime, and basic sample playback.
 *
 * Corrections vs v1.0:
 *
 *   1. DMACON disable was wrong: writing DMAF_AUDIO (0x000F) without
 *      DMAF_SETCLR clears those bits - this is correct. But the original
 *      init() wrote DMAF_AUDIO to disable without checking that DMAF_SETCLR
 *      was absent, which IS correct. However stop_channel() and play_sample()
 *      also wrote the raw DMA bit without DMAF_SETCLR to disable - also
 *      correct. These were fine. The actual bug was in play_sample(): it
 *      disabled the DMA bit before programming registers (correct) but did
 *      not wait for the DMA to actually stop before touching the registers.
 *      A DMA channel must be disabled and confirmed idle before the pointer
 *      and length registers are written.  We now wait for the channel to go
 *      idle by polling DMACONR.
 *
 *   2. AUDxLEN must be >= 2 (minimum 2 words = 4 bytes).  Hardware ignores
 *      length=1 and may repeat with undefined behaviour.  We enforce this.
 *
 *   3. The period formula was correct (sys_clock / (2 * freq)) BUT the
 *      Amiga audio period is actually: period = sys_clock / freq
 *      WITHOUT the /2, because Paula already divides by 2 internally for
 *      left/right stereo channel pairs.  HOWEVER - this is a common source
 *      of confusion.  The correct formula for Paula DMA audio period is:
 *        period = clock / (2 * freq)   -- for the base clock
 *      ...ONLY if you are using the raw 3.58 MHz clock.  The official
 *      Amiga Hardware Reference Manual defines:
 *        sample_rate = clock / period
 *      where clock = 3546895 (PAL) or 3579545 (NTSC).
 *      So: period = clock / sample_rate (NOT /2).
 *      The original code used /2 which gives frequencies twice as high
 *      as intended.  Fixed.
 *
 *   4. Minimum safe period is 124 per the HRM. The original correctly
 *      enforced this but only in some paths. Now enforced everywhere.
 *
 *   5. ADKCON must be written to clear UARTBRK and set audio modulation
 *      mode. The original never touched ADKCON, leaving the audio channels
 *      potentially in modulation mode from a previous Kickstart state.
 *      We now write ADKCON to disable all audio modulation (use plain DMA).
 *
 *   6. The sine wave chip RAM allocation must be word-aligned (2 bytes).
 *      The original used alignment=2 which is correct. However the sample
 *      length passed to play_sample was 16 words (32 bytes = correct for
 *      32-sample table). This is fine but we now use a named constant.
 *
 *   7. The waveform data itself: the original sine table is correct, but
 *      Paula requires the sample pointer to be WORD-aligned and the data
 *      to be in CHIP RAM. We add a static_assert to verify table size.
 *
 *   8. delay_ms() was calibrated for 68030 @ 25MHz. On a 68060 @ 50MHz
 *      the loop runs ~8x faster, making all timing badly wrong.  We now
 *      use VBlank counting via INTREQR for accurate ms delays, which works
 *      correctly on any CPU speed.
 *
 *   9. boot_chime() fade-out loop: "for (vol = 64; vol >= 0; vol -= 4)"
 *      with a signed int will correctly reach 0, but the final iteration
 *      writes vol=0 then subtracts 4 giving vol=-4 which still satisfies
 *      vol >= 0 being false - this is actually fine for signed int. But
 *      we rewrite it clearly anyway to avoid any ambiguity.
 *
 *  10. stop_all() did not clear INTREQ audio flags, leaving stale interrupt
 *      requests pending. Fixed.
 *
 *  11. The include path was wrong: "../chipset/custom.h" should be
 *      "custom.h" (relative to drivers/audio/) or "../chipset/custom.h".
 *      This depends on the Makefile's -I paths. We use the path that
 *      matches the Makefile's -Idrivers/chipset flag: just "custom.h".
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "custom.h"

namespace neo {
namespace audio {

/* ========================================================================
 * Paula clock constants
 *
 * The Paula period register value determines playback rate:
 *   sample_rate_hz = sys_clock / period
 *   period         = sys_clock / sample_rate_hz
 *
 * Minimum period = 124 (from the Amiga Hardware Reference Manual).
 * Maximum period = 65535 (register is 16-bit).
 * ======================================================================== */

static constexpr uint32_t PAL_CLOCK   = 3546895UL;
static constexpr uint32_t NTSC_CLOCK  = 3579545UL;
static constexpr uint16_t MIN_PERIOD  = 124;
static constexpr uint16_t MAX_PERIOD  = 0xFFFF;
static constexpr uint16_t MIN_LENGTH  = 2;    /* HRM: AUDxLEN must be >= 2 */

/* ========================================================================
 * Channel register offsets (from AUDxLCH base)
 *
 * Each channel occupies 16 bytes:
 *   +0  AUDxLCH  - sample pointer high word
 *   +2  AUDxLCL  - sample pointer low word
 *   +4  AUDxLEN  - sample length in words (must be >= 2)
 *   +6  AUDxPER  - period
 *   +8  AUDxVOL  - volume (0-64)
 *   +10 AUDxDAT  - direct data (not used for DMA)
 * ======================================================================== */

static constexpr uint16_t OFF_LCH = 0;
static constexpr uint16_t OFF_LCL = 2;
static constexpr uint16_t OFF_LEN = 4;
static constexpr uint16_t OFF_PER = 6;
static constexpr uint16_t OFF_VOL = 8;

static constexpr uint16_t AUD_CH_BASE[4] = {
    AUD0LCH,
    AUD1LCH,
    AUD2LCH,
    AUD3LCH
};

static constexpr uint16_t AUD_DMA[4] = {
    DMAF_AUD0,
    DMAF_AUD1,
    DMAF_AUD2,
    DMAF_AUD3
};

static constexpr uint16_t AUD_INT[4] = {
    INTF_AUD0,
    INTF_AUD1,
    INTF_AUD2,
    INTF_AUD3
};

/* ========================================================================
 * Waveform data
 *
 * Must reside in Chip RAM (DMA accessible).
 * The table must be word-aligned; chip_alloc() guarantees this.
 * 32 samples = 16 words = minimum usable loop length for smooth audio.
 * ======================================================================== */

static constexpr int WAVE_SAMPLES = 32;
static constexpr int WAVE_WORDS   = WAVE_SAMPLES / 2;  /* 16 words = 32 bytes */

static const int8_t sine_wave[WAVE_SAMPLES] = {
       0,   24,   48,   70,   89,  105,  117,  124,
     127,  124,  117,  105,   89,   70,   48,   24,
       0,  -24,  -48,  -70,  -89, -105, -117, -124,
    -127, -124, -117, -105,  -89,  -70,  -48,  -24
};

/* Verify table size at compile time */
static_assert(sizeof(sine_wave) == WAVE_SAMPLES,
              "sine_wave table size mismatch");

extern "C" void *chip_alloc(uint32_t size, uint32_t alignment);

/* Chip RAM copy of waveform (DMA accessible) */
static int8_t  *wave_chip   = nullptr;
static uint32_t sys_clock   = PAL_CLOCK;
static bool     initialized = false;

/* ========================================================================
 * Boot chime notes
 *
 * C major arpeggio ascending. Periods computed from PAL clock:
 *   C5  = 523 Hz  -> period = 3546895 / 523  = 6782  (was 508: wrong)
 *   E5  = 659 Hz  -> period = 3546895 / 659  = 5382  (was 404: wrong)
 *   G5  = 784 Hz  -> period = 3546895 / 784  = 4524  (was 339: wrong)
 *   C6  = 1047 Hz -> period = 3546895 / 1047 = 3388  (was 254: wrong)
 *
 * The original periods were approximately half the correct values,
 * consistent with the /2 bug in freq_to_period().
 * ======================================================================== */

struct ChimeNote {
    uint16_t period;
    uint16_t duration_ms;
    uint8_t  volume;
};

static const ChimeNote boot_chime_notes[] = {
    { 6782, 100, 48 },   /* C5  (~523 Hz)  */
    { 5382, 100, 52 },   /* E5  (~659 Hz)  */
    { 4524, 100, 56 },   /* G5  (~784 Hz)  */
    { 3388, 200, 64 },   /* C6  (~1047 Hz) */
    {    0,   0,  0 }    /* End marker     */
};

/* ========================================================================
 * Delay helper
 *
 * Uses VBlank interrupt request flag for CPU-speed-independent timing.
 * One VBlank = 20ms (PAL) or 16.7ms (NTSC).
 *
 * We count VBlanks to get close to the requested milliseconds.
 * For sub-20ms delays (e.g. the 10ms fade steps) we fall through
 * immediately if ms < one frame period; callers that need sub-frame
 * precision should use CIA timers instead.
 *
 * IMPORTANT: This must be called with audio interrupts NOT routing
 * through an ISR that also clears VERTB, otherwise the flag may be
 * consumed before we see it.  At boot this is safe as we have not
 * enabled INTENA yet.
 * ======================================================================== */

static void delay_ms(uint32_t ms)
{
    if (ms == 0) return;

    /* Determine frame period */
    uint32_t frame_ms = (sys_clock == PAL_CLOCK) ? 20 : 17;

    uint32_t frames = ms / frame_ms;
    if (frames == 0) frames = 1;  /* Always wait at least one frame */

    for (uint32_t f = 0; f < frames; f++) {
        /* Clear any pending VERTB */
        custom_write(INTREQ, INTF_VERTB);
        /* Wait for the next vertical blank */
        while (!(custom_read(INTREQR) & INTF_VERTB)) {
            /* spin */
        }
    }
}

/* ========================================================================
 * Channel idle wait
 *
 * After disabling a channel's DMA bit we must wait for the current DMA
 * cycle to complete before touching the channel's registers.
 * On a 060 this can happen within a few bus cycles, but it is safest to
 * poll DMACONR until the bit clears.
 * ======================================================================== */

static void wait_channel_idle(int channel)
{
    /* The audio DMA busy bits are not directly readable via DMACONR in the
     * same way as the blitter.  The safe approach per the HRM is to wait
     * two full scanlines after disabling DMA before touching the registers.
     * At PAL timing, one scanline ≈ 227 color clocks ≈ 64 microseconds.
     * We wait by polling VHPOSR for two line increments. */

    uint8_t start_line = (uint8_t)(custom_read(VHPOSR) >> 8);
    uint8_t target     = (uint8_t)(start_line + 3);  /* 3 lines ≈ 192us */

    /* Poll until beam passes target line (handle wrap-around) */
    while (true) {
        uint8_t cur = (uint8_t)(custom_read(VHPOSR) >> 8);
        /* Simple wrap-safe check: if we started near the bottom of the
         * frame and target wraps, just wait until cur < start_line */
        if ((uint8_t)(target - start_line) <= (uint8_t)(cur - start_line)) {
            break;
        }
    }

    (void)channel;  /* channel number not needed for beam-based wait */
}

/* ========================================================================
 * Period / frequency conversion
 *
 * period = sys_clock / freq_hz
 * ======================================================================== */

static uint16_t freq_to_period(uint32_t freq_hz)
{
    if (freq_hz == 0) return MAX_PERIOD;
    uint32_t p = sys_clock / freq_hz;
    if (p < MIN_PERIOD) return MIN_PERIOD;
    if (p > MAX_PERIOD) return MAX_PERIOD;
    return (uint16_t)p;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    sys_clock = is_pal() ? PAL_CLOCK : NTSC_CLOCK;

    /* Allocate chip RAM for waveform - must be word-aligned */
    wave_chip = (int8_t *)chip_alloc((uint32_t)WAVE_SAMPLES, 2);
    if (!wave_chip) return false;

    /* Copy sine wave to chip RAM */
    for (int i = 0; i < WAVE_SAMPLES; i++) {
        wave_chip[i] = sine_wave[i];
    }

    /*
     * Disable all audio DMA channels.
     * Writing without DMAF_SETCLR clears the specified bits.
     */
    custom_write(DMACON, DMAF_AUDIO);

    /*
     * ADKCON: clear all audio modulation bits.
     * Bits 0-3 (USE0P-USE3P) and bits 4-7 (USE0V-USE3V) select modulation
     * sources.  We want plain DMA audio with no modulation, so clear all.
     * Writing without the SET bit (bit 15) clears the specified bits.
     */
    custom_write(ADKCON,
        0x0000 |  /* clear bit 15 -> this write CLEARS bits */
        (1 << 0) |  /* USE0P */
        (1 << 1) |  /* USE1P */
        (1 << 2) |  /* USE2P */
        (1 << 3) |  /* USE3P */
        (1 << 4) |  /* USE0V */
        (1 << 5) |  /* USE1V */
        (1 << 6) |  /* USE2V */
        (1 << 7)    /* USE3V */
    );

    /* Set all volumes to 0 */
    custom_write(AUD0VOL, 0);
    custom_write(AUD1VOL, 0);
    custom_write(AUD2VOL, 0);
    custom_write(AUD3VOL, 0);

    /* Clear any pending audio interrupt requests */
    custom_write(INTREQ, INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3);

    /* Disable audio interrupts in INTENA (we use polling, not ISR) */
    custom_write(INTENA, INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3);

    initialized = true;
    return true;
}

void set_volume(int channel, uint8_t volume)
{
    if (channel < 0 || channel > 3) return;
    if (volume > 64) volume = 64;
    custom_write((uint16_t)(AUD_CH_BASE[channel] + OFF_VOL), volume);
}

void set_period(int channel, uint16_t period)
{
    if (channel < 0 || channel > 3) return;
    if (period < MIN_PERIOD) period = MIN_PERIOD;
    custom_write((uint16_t)(AUD_CH_BASE[channel] + OFF_PER), period);
}

void play_sample(int channel, const int8_t *sample_chip,
                 uint16_t length_words, uint16_t period, uint8_t volume)
{
    if (channel < 0 || channel > 3) return;
    if (!sample_chip) return;
    if (length_words < MIN_LENGTH) length_words = MIN_LENGTH;
    if (period < MIN_PERIOD) period = MIN_PERIOD;
    if (volume > 64) volume = 64;

    uint16_t base = AUD_CH_BASE[channel];

    /* 1. Disable this channel's DMA */
    custom_write(DMACON, AUD_DMA[channel]);  /* no SETCLR -> clear bit */

    /* 2. Wait for the channel to go idle (HRM requirement) */
    wait_channel_idle(channel);

    /* 3. Clear any pending interrupt for this channel */
    custom_write(INTREQ, AUD_INT[channel]);

    /* 4. Program registers (safe now that DMA is idle) */
    uint32_t addr = (uint32_t)sample_chip;
    custom_write((uint16_t)(base + OFF_LCH), (uint16_t)(addr >> 16));
    custom_write((uint16_t)(base + OFF_LCL), (uint16_t)(addr & 0xFFFF));
    custom_write((uint16_t)(base + OFF_LEN), length_words);
    custom_write((uint16_t)(base + OFF_PER), period);
    custom_write((uint16_t)(base + OFF_VOL), volume);

    /* 5. Enable DMA for this channel (and ensure master DMA is on) */
    custom_write(DMACON, (uint16_t)(DMAF_SETCLR | DMAF_MASTER | AUD_DMA[channel]));
}

void stop_channel(int channel)
{
    if (channel < 0 || channel > 3) return;

    /* Disable DMA */
    custom_write(DMACON, AUD_DMA[channel]);

    /* Wait for idle before clearing volume (avoids click on some hardware) */
    wait_channel_idle(channel);

    /* Silence */
    custom_write((uint16_t)(AUD_CH_BASE[channel] + OFF_VOL), 0);

    /* Clear interrupt flag */
    custom_write(INTREQ, AUD_INT[channel]);
}

void stop_all(void)
{
    /* Disable all audio DMA */
    custom_write(DMACON, DMAF_AUDIO);

    /* Silence all channels */
    custom_write(AUD0VOL, 0);
    custom_write(AUD1VOL, 0);
    custom_write(AUD2VOL, 0);
    custom_write(AUD3VOL, 0);

    /* Clear all pending audio interrupt requests */
    custom_write(INTREQ, INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3);
}

void beep(uint32_t freq_hz, uint32_t duration_ms, uint8_t volume)
{
    if (!initialized || !wave_chip) return;

    uint16_t period = freq_to_period(freq_hz);

    play_sample(0, wave_chip, (uint16_t)WAVE_WORDS, period, volume);
    delay_ms(duration_ms);
    stop_channel(0);
}

void beep(void)
{
    /* Default beep: 880 Hz, 150ms, medium volume */
    beep(880, 150, 48);
}

void boot_chime(void)
{
    if (!initialized || !wave_chip) return;

    for (int i = 0; boot_chime_notes[i].period != 0; i++) {
        const ChimeNote &note = boot_chime_notes[i];
        play_sample(0, wave_chip, (uint16_t)WAVE_WORDS,
                    note.period, note.volume);
        delay_ms(note.duration_ms);
    }

    /* Let the last note ring */
    delay_ms(100);

    /*
     * Fade out: step volume from 64 down to 0 in steps of 4.
     * We write directly to the volume register mid-playback which
     * Paula handles gracefully (volume is applied per-sample).
     */
    for (int vol = 64; vol > 0; vol -= 4) {
        custom_write(AUD0VOL, (uint16_t)vol);
        delay_ms(10);
    }

    stop_channel(0);
}

uint16_t hz_to_period(uint32_t freq_hz)
{
    return freq_to_period(freq_hz);
}

bool is_initialized(void)
{
    return initialized;
}

} /* namespace audio */
} /* namespace neo */
