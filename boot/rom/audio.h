#ifndef NB_AUDIO_H
#define NB_AUDIO_H

/*
 * Paula playback: one channel, one buffer, started once per sound.
 *
 * The startup chime is copied into chip RAM at NB_SND_BASE before it is
 * played, because Paula is a DMA master and fetches from nowhere else:
 * the sample lives in the ROM image at $F80000 on the reset build and in
 * whatever memory LoadSeg() chose on the chain build, neither of which
 * the chipset can reach.  NB_SND_BASE sits above the BSS and below the
 * stack in the chip RAM layout rom.ld describes, and will want to become
 * a real allocation when the takeover allocates the frame buffer too.
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
 * The rate is read out of the header rather than computed: the link has
 * no libgcc, so a division by a runtime value would not build.
 */

#define NB_SND_BASE 0x00120000UL      /* chip RAM, $110000 BSS .. stack   */
#define NB_SND_MAX  0x00020000UL      /* 128 KiB, the audio device's own  */

/* Drop any completion left over from whoever programmed the channel. */
void nb_sound_init(void);

/*
 * Copy an NSND image into chip RAM and start it on channel 0.  vol is
 * Paula's own 0..64 scale and is clamped to it.  Returns the sample's
 * rate in Hz, or 0 if the image is not a playable NSND -- in which case
 * nothing has been armed and the channel is left as it was.
 */
unsigned nb_sound_play(const void *src, unsigned bytes, unsigned vol);

/* Print ">sound done" once, on serial, when the channel runs dry. */
void nb_sound_poll(void);

#endif /* NB_AUDIO_H */
