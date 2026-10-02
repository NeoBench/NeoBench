/*
 * Host test of the NCR53C710 driver (boot/rom/scsi.c).
 *
 * The chip itself cannot be conjured up on the host, and only an A4000T
 * has one -- but every decision the driver makes before it touches a
 * register is arithmetic, and that arithmetic is where both of the
 * failures that actually happened lived.  The register decode is checked
 * against the window FS-UAE's Gayle decodes ($DD0040 and $DD0080 with
 * the board handing the bytes over in the other order), the SCRIPTS
 * encodings against the instruction words lsi53c710.cpp decodes them
 * with, and the READ(10)/WRITE(10) layout against the SCSI-2 wording --
 * a length written into the wrong pair of bytes is a legal command
 * asking for nothing, and one that completes without moving any data, so
 * nothing downstream ever notices it was wrong.
 *
 *   cc -O2 -Wall -Wextra -o test_scsi test_scsi.c
 *   ./test_scsi
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * The refusal line writes to a serial port that does not exist here.
 * scsi.c is included rather than linked so that the static helpers and
 * the macros above them are in scope; the driver's own entry points are
 * never called, so no address in $DD0000 is ever read.
 */
#include "../../boot/rom/scsi.c"

void amiga_serial_putc(char c)
{
    (void)c;
}

static int failures;

static void check(int ok, const char *what)
{
    if (!ok)
    {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void check_u(unsigned got, unsigned want, const char *what)
{
    if (got != want)
    {
        failures++;
        printf("FAIL: %s: got $%x, want $%x\n", what, got, want);
    }
}

static void check_s(const char *got, const char *want, const char *what)
{
    if (got == NULL || strcmp(got, want) != 0)
    {
        failures++;
        printf("FAIL: %s: got \"%s\", want \"%s\"\n",
               what, got ? got : "(null)", want);
    }
}

/*
 * How a register number reaches the chip.
 *
 * The board hands the NCR its bytes in the other order from a 68k
 * longword, so register R lives at window + (R xor 3) -- which is what
 * puts CTEST1, the register the probe reads first, at $DD0056 in the
 * primary bank and $DD0096 in the alternate.  Anything else in the
 * window reads as something the probe would not mistake for the chip.
 */
static void test_decode(void)
{
    unsigned win;
    unsigned reg;
    const unsigned windows[] = { NCR_WIN0, NCR_WIN1 };

    check_u(NCR_WIN0, 0x40, "primary window offset");
    check_u(NCR_WIN1, 0x80, "alternate window offset");

    for (unsigned w = 0; w < sizeof windows / sizeof windows[0]; w++)
    {
        win = windows[w];
        for (reg = 0; reg < 0x40; reg++)
        {
            uintptr_t addr = (uintptr_t)reg_at(win, reg);

            check(addr == NCR_MEM + win + (reg ^ 3u),
                  "reg_at: address arithmetic");
            check(((addr - NCR_MEM - win) ^ 3u) == reg,
                  "reg_at: the address decodes back to the register");
            check(addr >= NCR_MEM + NCR_WIN0 && addr < NCR_MEM + NCR_WIN1 + 0x40,
                  "reg_at: the byte lands inside the banked window");
        }
    }

    check_u((unsigned)((uintptr_t)reg_at(NCR_WIN0, R_CTEST1) - 0x00dd0000ul),
            0x56, "CTEST1 reads at $DD0056");
    check_u((unsigned)((uintptr_t)reg_at(NCR_WIN1, R_CTEST1) - 0x00dd0000ul),
            0x96, "CTEST1 reads at $DD0096");
    check_u((unsigned)((uintptr_t)reg_at(NCR_WIN0, R_ISTAT) - 0x00dd0000ul),
            0x62, "ISTAT reads at $DD0062");
}

/*
 * The instruction words, held to the encoding lsi53c710.cpp decodes:
 * a block move carries its phase in bits 24..26 with the byte count
 * below, a select carries a target bit mask in bits 16..23 with bit 24
 * asking for ATN, and the transfer control that ends the program picks
 * its opcode out of bits 27..25 with bit 19 making it unconditional.
 */
static void test_scripts(void)
{
    const unsigned phases[] = { P_DO, P_DI, P_CMD, P_ST, P_MO, P_MI };
    const unsigned want[]   = { 0, 1, 2, 3, 6, 7 };
    unsigned i;

    for (i = 0; i < sizeof phases / sizeof phases[0]; i++)
        check_u(phases[i], want[i], "SCSI phase number");

    check_u(SCN_MOVE(P_DI, 512), 0x01000200u, "block move: data in, 512");
    check_u(SCN_MOVE(P_DO, 512), 0x00000200u, "block move: data out, 512");
    check_u(SCN_MOVE(P_CMD, 10), 0x0200000au, "block move: command, 10");
    check_u(SCN_MOVE(P_ST, 1),   0x03000001u, "block move: status, 1");
    check_u(SCN_MOVE(P_MO, 1),   0x06000001u, "block move: message out, 1");
    check_u(SCN_MOVE(P_MI, 1),   0x07000001u, "block move: message in, 1");

    check_u((SCN_MOVE(P_DI, 512) >> 24) & 7u, P_DI,
            "block move phase is where the emulator looks for it");
    check_u(SCN_MOVE(P_DI, 512) & 0x00ffffffu, 512,
            "block move count sits below the phase");

    check_u(SCN_SELECT(0, 1), 0x41010000u, "select: target 0 with ATN");
    check_u(SCN_SELECT(7, 1), 0x41800000u, "select: target 7 with ATN");
    check_u(SCN_SELECT(0, 0), 0x40010000u, "select: target 0 without ATN");
    check_u(SCN_SELECT(3, 0), 0x40080000u, "select: target 3 without ATN");
    check_u(SCN_SELECT(5, 1) & 0x00ff0000u, 1u << 21,
            "select: target is a bit mask in bits 16..23");
    check_u((SCN_SELECT(0, 1) >> 30) & 3u, 1, "select: instruction class");
    /*
     * Bit 24 is ATN and nothing else, so it has to be the only thing
     * that moves when the argument does -- it was once baked into the
     * base word, which left the argument free to make no difference at
     * all while every program still selected with ATN asserted.
     */
    check_u(SCN_SELECT(2, 1) ^ SCN_SELECT(2, 0), 0x01000000u,
            "select: ATN is exactly bit 24");

    check_u(SCN_STOP, 0x98080000u, "terminator");
    check_u((SCN_STOP >> 30) & 3u, 2, "terminator: transfer control class");
    check_u((SCN_STOP >> 27) & 7u, 3, "terminator: interrupt opcode");
    check((SCN_STOP & (1u << 19)) != 0, "terminator: bit 19 set (unconditional)");
    check((SCN_STOP & (1u << 20)) == 0, "terminator: bit 20 clear (no jump)");

    /*
     * The count field is 24 bits wide, so the largest transfer one
     * program can move is bounded by it rather than by the device:
     * SECTORS_MAX sectors have to fit below the phase bits.
     */
    check_u(SECTORS_MAX, 32767u, "largest transfer, in sectors");
    check(SECTORS_MAX * 512u <= 0x00ffffffu,
          "SECTORS_MAX sectors fit in a block move count");
}

/*
 * The phases as the chip reports them back, which is the one thing the
 * refusal line translates: SSTAT2 carries the phase the bus was actually
 * in in its low three bits, and the names are the chip's own.
 */
static void test_phase_names(void)
{
    check_s(phase_name(0), "do",  "phase 0");
    check_s(phase_name(1), "di",  "phase 1");
    check_s(phase_name(2), "cmd", "phase 2");
    check_s(phase_name(3), "st",  "phase 3");
    check_s(phase_name(6), "mo",  "phase 6");
    check_s(phase_name(7), "mi",  "phase 7");
    check_s(phase_name(4), "?",   "phase 4 is not a bus phase");
    check_s(phase_name(5), "?",   "phase 5 is not a bus phase");
    /* SSTAT2 carries the phase in its low three bits and other status above */
    check_s(phase_name(0x80 | 1), "di", "only the low three bits name a phase");
    check_s(phase_name(0xff), "mi", "the bits above the phase are ignored");
}

/*
 * READ(10) and WRITE(10): opcode, flags, a 32 bit address in bytes 2..5,
 * the transfer length in bytes 7..8, control in byte 9.
 *
 * Bytes 7..8 and not 8..9 -- a length written one byte too far is a
 * command asking for nothing, and a command asking for nothing still
 * completes, so the program waiting for data stalls in a phase the chip
 * will not see again.  That is the bug this section exists for.
 */
static void test_cdb(void)
{
    uint8_t c[10];
    unsigned i;

    rw_cdb(c, 0x00010203ul, 255u, 0);
    check_u(c[0], 0x28, "READ(10) opcode");
    check_u(c[1], 0,    "READ(10) flags");
    check_u(c[2], 0x00, "READ(10) address byte 2");
    check_u(c[3], 0x01, "READ(10) address byte 3");
    check_u(c[4], 0x02, "READ(10) address byte 4");
    check_u(c[5], 0x03, "READ(10) address byte 5");
    check_u(c[6], 0,    "group number byte is left clear");
    check_u(c[7], 0x00, "READ(10) length, high byte");
    check_u(c[8], 0xff, "READ(10) length, low byte");
    check_u(c[9], 0,    "control byte is left clear");

    rw_cdb(c, 0x89abcdeful, 1u, 1);
    check_u(c[0], 0x2a, "WRITE(10) opcode");
    check_u(c[2], 0x89, "WRITE(10) address byte 2");
    check_u(c[3], 0xab, "WRITE(10) address byte 3");
    check_u(c[4], 0xcd, "WRITE(10) address byte 4");
    check_u(c[5], 0xef, "WRITE(10) address byte 5");
    check_u(c[7], 0,    "one sector: length high byte");
    check_u(c[8], 1,    "one sector: length low byte");

    /* the case that stalled: 512 bytes is a length of 2, not of 0 */
    rw_cdb(c, 0ul, 512u, 0);
    check_u(c[7], 2, "512 sectors: length high byte");
    check_u(c[8], 0, "512 sectors: length low byte");
    check(c[7] != 0 || c[8] != 0, "a nonzero length is never encoded as zero");

    rw_cdb(c, 0ul, SECTORS_MAX, 1);
    check_u(c[7], 0x7f, "SECTORS_MAX: length high byte");
    check_u(c[8], 0xff, "SECTORS_MAX: length low byte");
    check_u(c[0], 0x2a, "WRITE(10) opcode at the largest transfer");

    /* nothing outside the defined fields may be set */
    rw_cdb(c, 0ul, 3u, 0);
    for (i = 0; i < 10; i++)
    {
        if (i == 0 || i == 7 || i == 8)
            continue;
        check_u(c[i], 0, "reserved CDB byte is clear");
    }
}

int main(void)
{
    test_decode();
    test_scripts();
    test_phase_names();
    test_cdb();

    if (failures)
    {
        printf("test_scsi: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_scsi: ok\n");
    return 0;
}
