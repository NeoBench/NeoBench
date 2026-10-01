#ifndef NB_AUDIO_H
#define NB_AUDIO_H

/*
 * Paula playback: one channel, one buffer at a time.
 *
 * The sample lives in chip RAM because Paula is a DMA master and
 * fetches from nowhere else: the ROM image at $F80000 on the reset
 * build and whatever memory LoadSeg() chose on the chain build are both
 * beyond it.  NB_SND_BASE sits above the BSS and well below the stack,
 * in the only stretch of the 2 MB nothing else claims, and rom.ld
 * asserts that _end does not reach it -- it used to: the buffer was at
 * $120000 with a 64 KiB allowance, the desktop's BSS grew to $127334,
 * and the startup chime wrote its samples over the tail of the desktop's
 * own state.  $150000..$1C0000 is 448 KiB of chip RAM for sound alone,
 * leaving 176 KiB for the BSS to grow into and 256 KiB of stack above
 * it.
 *
 * Two callers: the boot plays an NSND image whole, and the player arms
 * runs of decoded PCM over whatever it has decoded into the same
 * buffer.  Either way one pass is one pass -- Agnus reloads the address
 * and the length when the counter runs out and plays the buffer again,
 * so a sound that is asked to sound once is a sound somebody has to
 * stop.  That somebody is nb_sound_poll(), called from the main loop;
 * the stop is timed from the field counter rather than taken from
 * AUD0's completion request, which is not usable on this machine (the
 * note at the top of audio.c says why, and what was printed when it was
 * found out).
 *
 * Samples are NSND: a 16 byte big endian header, then signed 8 bit mono
 * data of even length.
 *
 *      0  "NSND"
 *      4  period    Paula period in color clock ticks
 *      6  channels  1
 *      8  length    sample bytes
 *     12  rate      samples per second, as generated
 *
 * A run armed directly carries its rate instead, and the period is
 * worked out here: a colour clock is 3 546 895 Hz, so Paula wants that
 * many over the rate, clamped to the 124 the hardware documents as its
 * floor.
 */

#define NB_SND_BASE 0x00150000UL      /* chip RAM, above BSS, below stack */
#define NB_SND_MAX  0x00070000UL      /* 448 KiB, $150000..$1C0000        */

/* Drop any completion left over from whoever programmed the channel. */
void nb_sound_init(void);

/*
 * Copy an NSND image into chip RAM and start it on channel 0, then wait
 * the pass out.  vol is Paula's own 0..64 scale and is clamped to it.
 * Returns the sample's rate in Hz, or 0 if the image is not a playable
 * NSND -- in which case nothing has been armed and the channel is left
 * as it was.
 */
unsigned nb_sound_play(const void *src, unsigned bytes, unsigned vol);

/*
 * Arm channel 0 over signed 8-bit mono PCM that is already in chip RAM
 * inside the sound buffer, and return without waiting: the main loop
 * goes on drawing and nb_sound_poll() lets go of the channel when the
 * pass is over.  `rate` is samples per second and is turned into
 * Paula's period here.  Returns the period used, or 0 -- nothing is
 * armed -- if the range is not one this file may touch, if the length
 * is odd, or if the rate is zero.
 */
unsigned nb_sound_start(unsigned addr, unsigned bytes,
                        unsigned rate, unsigned vol);

/* Let go of the channel now, whatever is left of the pass. */
void nb_sound_stop(void);

/* 1 while a run is still in flight, 0 once it has been let go. */
int nb_sound_busy(void);

/* Print ">sound done" once, on serial, when the channel runs dry. */
void nb_sound_poll(void);

#endif /* NB_AUDIO_H */
