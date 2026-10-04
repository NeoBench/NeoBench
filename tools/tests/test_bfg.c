/*
 * Host test of the BFG9060 signature decoder (boot/rom/probe.c).
 *
 * The card sits in the CPU slot of an A3000 or an A4000, so neither the
 * emulator this system is booted in nor the machine building it has
 * one: what can be held against the code is the word.  The card's own
 * bootrom goes looking for $BF690600 with the firmware version in its
 * low nibble, at $FF040000, sixty-four longs deep (Bootrom/bootrom.s,
 * FW_MAGIC_ID and FW_SCAN_SIZE, in the BFG9060 project) -- so every
 * word that would be a card is here, and so is everything that address
 * is more likely to hold: open bus either way round, the memory probe's
 * two patterns and its megabyte marker, the same signature shifted the
 * way the bootrom holds it beside this one, byte-reversed, and one bit
 * set wrong.  The decoder is reached by including probe.c, which needs
 * the three instruction probes and the display check to be standing in
 * for hardware that is not here; the window read itself cannot run on a
 * host and is not attempted, so what this proves is the decision the
 * read hands its answer to.
 *
 *   cc -O2 -Wall -Wextra -std=c99 -o test_bfg test_bfg.c
 *   ./test_bfg
 */
#include <stdio.h>
#include <stdint.h>

/* What probe.c would find in fline.S and amiga.c. */
int      nb_probe_move16(void)     { return 0; }
int      nb_probe_pcr(void)        { return 0; }
unsigned nb_probe_mmu(void)        { return 0; }
int      amiga_display_ready(void) { return 1; }

#include "../../boot/rom/probe.c"

static int failures;

static void check_version(uint32_t word, unsigned want)
{
    int fw = bfg9060_fw(word);

    if (fw != (int)want)
    {
        failures++;
        printf("FAIL: $%08lx: firmware %d, want %u\n",
               (unsigned long)word, fw, want);
    }
}

static void check_nothing(uint32_t word, const char *what)
{
    int fw = bfg9060_fw(word);

    if (fw != -1)
    {
        failures++;
        printf("FAIL: %s ($%08lx) answered firmware %d, want nothing\n",
               what, (unsigned long)word, fw);
    }
}

int main(void)
{
    unsigned v;

    /* Every word the card itself can hold: the signature, and one of
     * the sixteen versions the low nibble can carry. */
    for (v = 0; v < 16; v++)
        check_version(0xbf690600UL | (uint32_t)v, v);

    /* And everything else that address is likely to answer. */
    check_nothing(0x00000000UL, "open bus, one way");
    check_nothing(0xffffffffUL, "open bus, the other");
    check_nothing(0x55aa33ccUL, "the memory probe's first pattern");
    check_nothing(0xaa55cc33UL, "the memory probe's second pattern");
    check_nothing(0x5a000000UL, "a counted megabyte's marker");
    check_nothing(0xbf690610UL, "one past the last version");
    check_nothing(0x0bf69060UL, "the signature already shifted");
    check_nothing(0x000669bfUL, "the signature byte for byte reversed");
    check_nothing(0xbf790600UL, "one bit of the signature set wrong");
    check_nothing(0xbf690000UL, "the signature with $0600 cleared");

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_bfg: all checks passed\n");
    return 0;
}
