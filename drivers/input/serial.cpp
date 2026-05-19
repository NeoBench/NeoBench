/*
 * NeoBench Bare-Metal Amiga Kernel
 * Serial Port Driver
 *
 * Uses Amiga custom chip serial registers (SERPER, SERDATR, SERDAT)
 * for RS-232 communication via the DB25 serial port. Default 9600 8N1.
 *
 * Corrections vs v1.0:
 *
 *  1. SERDATR BIT DEFINITIONS WERE WRONG (critical).
 *     The original had contradictory comments then corrected itself
 *     mid-definition, but the final values were right. However the
 *     first constant "SERDATR_RBF = 0x4000" was wrong (bit 14 is RXD,
 *     not RBF). The corrected block below was also in the original.
 *     We remove the dead first definition and keep only the correct ones.
 *     Correct SERDATR bits per the HRM:
 *       Bit 15  OVRUN  - overrun error (received before prev read)
 *       Bit 14  RXD    - current state of receive pin
 *       Bit 13  TSRE   - transmit shift register empty
 *       Bit 12  TBE    - transmit buffer empty (safe to write SERDAT)
 *       Bit 11  RBF    - receive buffer full (data ready to read)
 *       Bit  8  STOPBIT- received stop bit value
 *       Bits 7:0 DATA  - received data byte
 *
 *  2. TBE WAIT LOGIC WRONG IN putchar().
 *     The original waited for SDR_TBE (bit 12) before writing. This is
 *     correct. HOWEVER it then cleared INTF_TBE via INTREQ. Clearing
 *     INTREQ.TBE while in a polling loop is harmless but unnecessary
 *     and slightly misleading. Worse: if interrupts are enabled and an
 *     ISR is also watching TBE, clearing it here creates a race. We
 *     remove the INTREQ clear from the polling path. The INTREQ flag
 *     is cleared automatically when SERDAT is written (the act of
 *     writing SERDAT loads the transmit buffer and TBE goes low, then
 *     high again when the byte has shifted out).
 *
 *  3. INIT() ENABLES TBE AND RBF INTERRUPTS IN INTENA.
 *     The original wrote INTENA with INTF_SETCLR | INTF_TBE | INTF_RBF.
 *     This enables both TBE and RBF interrupts globally at init time.
 *     TBE fires immediately (the transmit buffer is empty at init),
 *     causing a spurious interrupt before any ISR is installed.
 *     RBF is fine to enable if there is an ISR.
 *     For a kernel that uses polling by default (as this one does), we
 *     should NOT enable INTENA for serial at init. The interrupt-driven
 *     path (rx_interrupt()) can be enabled explicitly by the caller.
 *     Fixed: init() does NOT touch INTENA. Added enable_rx_interrupt()
 *     and disable_rx_interrupt() functions for callers that want ISR mode.
 *
 *  4. BAUD RATE FORMULA.
 *     SERPER = (clock / baud) - 1. The original had this correct.
 *     However the original did not check for division by zero or for
 *     the result exceeding 15 bits (SERPER is 15 bits, bit 15 is LONG
 *     mode flag). Added bounds check.
 *
 *  5. OVERRUN DETECTION.
 *     The original never checked OVRUN. If getchar() is called slowly
 *     and a second byte arrives before the first is read, OVRUN is set
 *     and the first byte is lost. We now check and report overrun.
 *     A production driver would push a special event; we just clear the
 *     condition by reading and note it in a counter.
 *
 *  6. getchar() IN POLLING MODE SHOULD CLEAR RBF INTERRUPT FLAG.
 *     After reading SERDATR the RBF flag in INTREQ should be cleared to
 *     prevent a stale interrupt from firing if ISR mode is later enabled.
 *     Fixed: clear INTREQ.RBF after polling read.
 *
 *  7. INCLUDE PATH: "../chipset/custom.h" should be "custom.h".
 *     Fixed.
 *
 *  8. SERPER BIT 15 = LONG MODE.
 *     The original masked with 0x7FFF before writing SERPER which
 *     correctly ensures 8-bit mode (LONG=0). Correct. No change.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "custom.h"

namespace neo {
namespace serial {

/* ========================================================================
 * Clock constants (same as Paula, defined independently here)
 * ======================================================================== */

static constexpr uint32_t PAL_CLOCK  = 3546895UL;
static constexpr uint32_t NTSC_CLOCK = 3579545UL;

/* ========================================================================
 * SERDATR status bits (read-only)
 *
 * Per Amiga Hardware Reference Manual:
 *   Bit 15  OVRUN  - overrun: new byte received before previous was read
 *   Bit 14  RXD    - current logic state of RXD pin
 *   Bit 13  TSRE   - transmit shift register empty (all bits shifted out)
 *   Bit 12  TBE    - transmit buffer empty (OK to write next byte to SERDAT)
 *   Bit 11  RBF    - receive buffer full (received byte waiting to be read)
 *   Bit  8  STOPBIT- value of the stop bit received (should be 1 for valid)
 *   Bits 7:0 DATA  - received data byte
 * ======================================================================== */

static constexpr uint16_t SDR_OVRUN  = 0x8000;
static constexpr uint16_t SDR_RXD    = 0x4000;
static constexpr uint16_t SDR_TSRE   = 0x2000;
static constexpr uint16_t SDR_TBE    = 0x1000;
static constexpr uint16_t SDR_RBF    = 0x0800;
static constexpr uint16_t SDR_STOPBIT= 0x0100;

/* ========================================================================
 * State
 * ======================================================================== */

static uint32_t current_baud = 9600;
static uint32_t sys_clock    = PAL_CLOCK;
static uint32_t overrun_count = 0;

/* Receive ring buffer (used by interrupt-driven path) */
static constexpr int RX_BUF_SIZE = INODE_SIZE;
static uint8_t      rx_buffer[RX_BUF_SIZE];
static volatile int rx_head = 0;
static volatile int rx_tail = 0;

/* ========================================================================
 * Baud rate period calculation
 *
 * SERPER[14:0] = (sys_clock / baud) - 1
 * Maximum period value = 0x7FFF (15 bits).
 * Minimum baud at PAL: 3546895 / 32767 ≈ 108 baud.
 * Maximum baud at PAL: 3546895 / 1 = 3546895 (unreachable in practice).
 * ======================================================================== */

static uint16_t calc_period(uint32_t baud)
{
    if (baud == 0) baud = 9600;
    uint32_t period = (sys_clock / baud);
    if (period == 0) period = 1;
    period -= 1;
    if (period > 0x7FFF) period = 0x7FFF;
    return (uint16_t)period;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(uint32_t baud)
{
    sys_clock    = is_pal() ? PAL_CLOCK : NTSC_CLOCK;
    current_baud = baud;

    /* Set baud rate, 8-bit mode (SERPER bit 15 = 0 = 8-bit) */
    custom_write(SERPER, calc_period(baud));

    /* Drain any stale received byte */
    if (custom_read(SERDATR) & SDR_RBF) {
        (void)custom_read(SERDATR);
    }

    /* Clear any pending serial interrupt flags */
    custom_write(INTREQ, INTF_TBE | INTF_RBF);

    /*
     * Do NOT enable INTENA for serial here.
     * Enabling TBE in INTENA immediately fires since TBE is asserted at
     * startup, causing a spurious interrupt before any ISR is installed.
     * Callers that want interrupt-driven receive should call
     * enable_rx_interrupt() after installing their ISR.
     */

    rx_head      = 0;
    rx_tail      = 0;
    overrun_count = 0;

    return true;
}

bool init(void)
{
    return init(9600);
}

void set_baud(uint32_t baud)
{
    current_baud = baud;
    custom_write(SERPER, calc_period(baud));
}

uint32_t get_baud(void)
{
    return current_baud;
}

/* Enable interrupt-driven receive (call after ISR is installed) */
void enable_rx_interrupt(void)
{
    custom_write(INTREQ, INTF_RBF);                    /* clear pending */
    custom_write(INTENA, INTF_SETCLR | INTF_RBF);      /* enable */
}

void disable_rx_interrupt(void)
{
    custom_write(INTENA, INTF_RBF);                    /* clear (no SETCLR) */
}

/* ========================================================================
 * Transmit - polling mode
 * ======================================================================== */

void putchar(char c)
{
    /*
     * Wait for TBE (Transmit Buffer Empty) before writing.
     * TBE = bit 12 of SERDATR. When set, the transmit buffer is ready
     * to accept a new byte.
     *
     * We do NOT clear INTREQ.TBE here. Writing SERDAT clears TBE
     * automatically (the hardware resets the flag when the buffer
     * is loaded). Clearing it manually in a polling loop would
     * be a no-op but could mask a real interrupt if ISR mode is
     * later enabled.
     */
    while (!(custom_read(SERDATR) & SDR_TBE)) {
        /* spin */
    }

    /* Write byte with stop bit in bit 8 (required for 8N1) */
    custom_write(SERDAT, (uint16_t)((uint8_t)c) | 0x0100);
}

void puts(const char *str)
{
    if (!str) return;
    while (*str) {
        if (*str == '\n') putchar('\r');
        putchar(*str++);
    }
}

/* ========================================================================
 * Receive - polling mode
 * ======================================================================== */

char getchar(void)
{
    /* Wait for RBF (Receive Buffer Full) */
    while (!(custom_read(SERDATR) & SDR_RBF)) {
        /* spin */
    }

    uint16_t data = custom_read(SERDATR);

    /* Check for overrun (arrived before previous byte was read) */
    if (data & SDR_OVRUN) {
        overrun_count++;
        /* The overrun byte is still valid; just note the error */
    }

    /* Clear RBF interrupt flag now that we have read the byte */
    custom_write(INTREQ, INTF_RBF);

    return (char)(data & 0xFF);
}

bool data_available(void)
{
    return (custom_read(SERDATR) & SDR_RBF) != 0;
}

uint32_t get_overrun_count(void)
{
    return overrun_count;
}

/* ========================================================================
 * Receive - interrupt-driven mode
 *
 * rx_interrupt() is called from the level-5 interrupt handler
 * (Paula / serial: INTREQ bit 11 = RBF shares the audio/serial vector).
 * The caller must have confirmed it is a serial RBF interrupt.
 * ======================================================================== */

void rx_interrupt(void)
{
    uint16_t status = custom_read(SERDATR);

    if (!(status & SDR_RBF)) return;  /* Not our interrupt */

    if (status & SDR_OVRUN) overrun_count++;

    uint8_t byte = (uint8_t)(status & 0xFF);

    int next_head = (rx_head + 1) % RX_BUF_SIZE;
    if (next_head != rx_tail) {
        rx_buffer[rx_head] = byte;
        rx_head = next_head;
    }
    /* Buffer full: drop byte (better than corrupting head/tail) */

    /* Clear RBF interrupt flag */
    custom_write(INTREQ, INTF_RBF);
}

char getchar_buffered(void)
{
    while (rx_head == rx_tail) { /* spin */ }
    char c  = (char)rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

bool buffered_available(void)
{
    return rx_head != rx_tail;
}

/* ========================================================================
 * Debug helpers
 * ======================================================================== */

void print_hex(uint32_t val)
{
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (int)((val >> i) & 0x0F);
        putchar(nibble < 10 ? (char)('0' + nibble) : (char)('A' + nibble - 10));
    }
}

void print_dec(uint32_t val)
{
    if (val == 0) { putchar('0'); return; }
    char buf[11];
    int  pos = 0;
    while (val > 0) {
        buf[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (pos > 0) putchar(buf[--pos]);
}

} /* namespace serial */
} /* namespace neo */
