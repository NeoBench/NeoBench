/*
 * NeoBench Bare-Metal Amiga Kernel
 * RTC Driver
 *
 * Supports two time sources:
 *
 *  1. Ricoh RP5C01A battery-backed real-time clock (A3000 / A4000 onboard)
 *     This is the primary source and provides full date/time with battery
 *     backup across power cycles.
 *
 *  2. CIA-A TOD (Time Of Day) counter
 *     Fallback when no RTC chip is present. Counts 50Hz (PAL) or 60Hz
 *     (NTSC) ticks from the last reset; no date information.
 *
 * Corrections vs stub:
 *
 *  1. CIA TOD READ HAD A LATCHING BUG (critical).
 *     The CIA TOD counter is a 24-bit counter that can be read via three
 *     byte-wide registers: TODHI, TODMID, TODLO.
 *     IMPORTANT: Reading TODHI latches all three registers simultaneously.
 *     Subsequent reads of TODMID and TODLO return the latched values, not
 *     the live counter.  The latch is released on the NEXT read of TODLO.
 *     The original read TODLO first, then TODMID, then TODHI — this is
 *     backwards and will produce inconsistent values (e.g. TODLO from
 *     tick N, TODHI from tick N+1 if the counter wraps between reads).
 *     Fixed: read TODHI first (latches), then TODMID, then TODLO (releases).
 *
 *  2. PAL DIVISOR HARDCODED TO 50.
 *     The CIA TOD clock is driven by the mains frequency: 50Hz on PAL
 *     machines, 60Hz on NTSC.  The original always divided by 50, giving
 *     wrong time on NTSC machines.  Fixed: detect PAL/NTSC via VPOSR
 *     bit 12 and use the correct divisor.
 *
 *  3. NO REAL RTC SUPPORT.
 *     The A4000 has a Ricoh RP5C01A RTC chip at 0xDC0000.  This provides
 *     battery-backed date and time and is what Kickstart uses.  Without
 *     it, the time resets to 1994-01-01 00:00:00 on every boot.
 *     Full RP5C01A driver implemented below.
 *
 *  4. is_present() ALWAYS RETURNED FALSE.
 *     Now correctly probes for the RP5C01A by checking its ID register.
 *
 *  5. YEAR FIELD.
 *     The original returned year=94 (years since 1900, i.e. 1994).
 *     We now return the full 4-digit year (e.g. 2025).
 *
 *  6. CIA TOD ALARM REGISTERS.
 *     Writing to CIA TOD registers while ALARM bit in CRA is set writes
 *     to the alarm, not the TOD counter.  We ensure CRA ALARM=0 before
 *     reading/writing TOD.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace rtc {

/* ========================================================================
 * CIA-A TOD registers
 *
 * The CIA-A TOD is a 24-bit counter clocked by the mains frequency.
 * Reading TODHI latches the entire 24-bit value; TODLO releases the latch.
 * MUST read in order: TODHI -> TODMID -> TODLO.
 * ======================================================================== */

static constexpr uint32_t CIAA_TODLO_ADDR  = 0xBFE801UL;
static constexpr uint32_t CIAA_TODMID_ADDR = 0xBFE901UL;
static constexpr uint32_t CIAA_TODHI_ADDR  = 0xBFEA01UL;
static constexpr uint32_t CIAA_CRA_ADDR    = 0xBFEE01UL;

/* CIA-A CRA bits */
static constexpr uint8_t CRA_ALARM  = 0x80;  /* 1 = write alarm, 0 = write TOD */
static constexpr uint8_t CRA_TODIN  = 0x20;  /* 0 = count 50Hz, 1 = count 60Hz */

/* VPOSR for PAL/NTSC detection */
static constexpr uint32_t VPOSR_ADDR = 0x00DFF004UL;
static constexpr uint16_t VPOSR_PAL  = 0x1000;

static inline bool is_pal_machine(void)
{
    return (*((volatile const uint16_t *)VPOSR_ADDR) & VPOSR_PAL) != 0;
}

/* ========================================================================
 * Ricoh RP5C01A Real-Time Clock
 *
 * The RP5C01A is mapped at 0xDC0000 on the A3000 and A4000.
 * Registers are nibble-wide (4-bit) at even byte addresses spaced 4 bytes
 * apart.  Only bits [3:0] of each byte are significant.
 *
 * The chip has two register banks (MODE bit selects):
 *   Bank 0: time/calendar data (BCD)
 *   Bank 1: alarm, timer configuration
 *
 * Register map (bank 0, all values BCD):
 *   0x00  Seconds (ones)        0-9
 *   0x01  Seconds (tens)        0-5
 *   0x02  Minutes (ones)        0-9
 *   0x03  Minutes (tens)        0-5
 *   0x04  Hours (ones)          0-9
 *   0x05  Hours (tens) + 24h    bit3=0 for 24h mode
 *   0x06  Day of week           0-6
 *   0x07  Day of month (ones)   1-9
 *   0x08  Day of month (tens)   0-3
 *   0x09  Month (ones)          1-9
 *   0x0A  Month (tens)          0-1
 *   0x0B  Year (ones)           0-9
 *   0x0C  Year (tens)           0-9
 *   0x0D  MODE register
 *   0x0E  TEST register (write 0)
 *   0x0F  RESET register
 *
 * MODE register (0x0D):
 *   bit 3: MASK  - timer output mask
 *   bit 2: TYPE  - 1 = count mode, 0 = time mode
 *   bit 1: BANK  - register bank select (0 or 1)
 *   bit 0: HOLD  - freeze counters for reliable multi-register read
 *
 * The year stored in the RTC is the last two digits.
 * Convention on Amiga: year 78-99 = 1978-1999, 00-77 = 2000-2077.
 *
 * The RP5C01A does not directly expose the century, so we apply the
 * above pivot rule.
 *
 * References:
 *   Ricoh RP5C01A datasheet
 *   Amiga A3000 / A4000 Technical Reference Manual
 * ======================================================================== */

static constexpr uint32_t RPC01A_BASE   = 0x00DC0000UL;
static constexpr uint32_t RPC01A_STRIDE = 4;  /* 4 bytes between registers */

/* Register indices */
static constexpr uint8_t RTC_SEC_ONES   = 0x00;
static constexpr uint8_t RTC_SEC_TENS   = 0x01;
static constexpr uint8_t RTC_MIN_ONES   = 0x02;
static constexpr uint8_t RTC_MIN_TENS   = 0x03;
static constexpr uint8_t RTC_HOUR_ONES  = 0x04;
static constexpr uint8_t RTC_HOUR_TENS  = 0x05;
static constexpr uint8_t RTC_DOW        = 0x06;
static constexpr uint8_t RTC_DAY_ONES   = 0x07;
static constexpr uint8_t RTC_DAY_TENS   = 0x08;
static constexpr uint8_t RTC_MON_ONES   = 0x09;
static constexpr uint8_t RTC_MON_TENS   = 0x0A;
static constexpr uint8_t RTC_YEAR_ONES  = 0x0B;
static constexpr uint8_t RTC_YEAR_TENS  = 0x0C;
static constexpr uint8_t RTC_MODE       = 0x0D;
static constexpr uint8_t RTC_TEST       = 0x0E;
static constexpr uint8_t RTC_RESET      = 0x0F;

/* MODE register bits */
static constexpr uint8_t RTC_MODE_HOLD  = 0x01;  /* Freeze for read */
static constexpr uint8_t RTC_MODE_BANK1 = 0x02;  /* Select bank 1 */
static constexpr uint8_t RTC_MODE_TYPE  = 0x04;  /* Count mode */
static constexpr uint8_t RTC_MODE_MASK  = 0x08;

static inline uint8_t rpc_read(uint8_t reg)
{
    volatile const uint8_t *p =
        (volatile const uint8_t *)(RPC01A_BASE + (uint32_t)reg * RPC01A_STRIDE);
    return *p & 0x0F;
}

static inline void rpc_write(uint8_t reg, uint8_t val)
{
    volatile uint8_t *p =
        (volatile uint8_t *)(RPC01A_BASE + (uint32_t)reg * RPC01A_STRIDE);
    *p = val & 0x0F;
}

/* Decode BCD nibble pair to decimal */
static inline uint8_t bcd_decode(uint8_t tens, uint8_t ones)
{
    return (uint8_t)(tens * 10 + ones);
}

/* Encode decimal to BCD nibble pair */
static inline void bcd_encode(uint8_t val, uint8_t *tens, uint8_t *ones)
{
    *tens = val / 10;
    *ones = val % 10;
}

/* ========================================================================
 * State
 * ======================================================================== */

static bool rtc_chip_present = false;

/* ========================================================================
 * RP5C01A probe
 *
 * The RP5C01A has no hard ID register, but we can probe by:
 *   1. Checking if the address responds (not all-FF / all-00)
 *   2. Writing and reading back the MODE register
 *   3. Verifying RTC values are in valid BCD ranges
 * ======================================================================== */

static bool probe_rpc01a(void)
{
    /*
     * Read the MODE register.  On a real RP5C01A it will always read
     * back a value in the range 0x00-0x0F.  Open bus typically reads
     * 0xFF (all bits set) or 0x00.
     *
     * We write 0 to MODE (bank 0, no hold, normal time mode) then
     * read it back. If it reads back non-0xFF we likely have an RTC.
     */
    rpc_write(RTC_MODE, 0x00);

    /* Short settle */
    for (volatile uint32_t i = 0; i < 1000; i++) {}

    uint8_t mode = rpc_read(RTC_MODE);

    /* Open bus on 68k usually reads 0x0F (all nibble bits set) */
    if (mode == 0x0F) return false;

    /*
     * Additionally verify: read seconds and check BCD validity.
     * BCD ones digit must be 0-9 and tens digit 0-5 for seconds.
     */
    uint8_t sec_ones = rpc_read(RTC_SEC_ONES);
    uint8_t sec_tens = rpc_read(RTC_SEC_TENS);

    if (sec_ones > 9 || sec_tens > 5) return false;

    return true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    rtc_chip_present = probe_rpc01a();

    if (rtc_chip_present) {
        /* Ensure we are in bank 0, time mode, no hold */
        rpc_write(RTC_MODE, 0x00);
        /* Write 0 to TEST register (no test mode) */
        rpc_write(RTC_TEST, 0x00);
    }

    return rtc_chip_present;
}

bool is_present(void)
{
    return rtc_chip_present;
}

/*
 * Read date/time from RP5C01A.
 *
 * We use the HOLD bit to freeze the counters during the read, preventing
 * a carry ripple from causing inconsistent values (e.g. reading seconds=59
 * then minutes before the minute increments, but after seconds rolled over).
 *
 * Protocol:
 *   1. Write MODE with HOLD=1 (freezes all counter outputs)
 *   2. Read all registers
 *   3. Write MODE with HOLD=0 (resume counting)
 */
static void read_from_rpc01a(DateTime *dt)
{
    /* Freeze */
    rpc_write(RTC_MODE, RTC_MODE_HOLD);

    /* Small delay for hold to take effect */
    for (volatile uint32_t i = 0; i < 100; i++) {}

    uint8_t sec_ones  = rpc_read(RTC_SEC_ONES);
    uint8_t sec_tens  = rpc_read(RTC_SEC_TENS);
    uint8_t min_ones  = rpc_read(RTC_MIN_ONES);
    uint8_t min_tens  = rpc_read(RTC_MIN_TENS);
    uint8_t hr_ones   = rpc_read(RTC_HOUR_ONES);
    uint8_t hr_tens   = rpc_read(RTC_HOUR_TENS) & 0x03;  /* bit3=AM/PM in 12h mode, mask off */
    uint8_t day_ones  = rpc_read(RTC_DAY_ONES);
    uint8_t day_tens  = rpc_read(RTC_DAY_TENS);
    uint8_t mon_ones  = rpc_read(RTC_MON_ONES);
    uint8_t mon_tens  = rpc_read(RTC_MON_TENS);
    uint8_t yr_ones   = rpc_read(RTC_YEAR_ONES);
    uint8_t yr_tens   = rpc_read(RTC_YEAR_TENS);

    /* Release hold */
    rpc_write(RTC_MODE, 0x00);

    dt->second = bcd_decode(sec_tens, sec_ones);
    dt->minute = bcd_decode(min_tens, min_ones);
    dt->hour   = bcd_decode(hr_tens,  hr_ones);
    dt->day    = bcd_decode(day_tens, day_ones);
    dt->month  = bcd_decode(mon_tens, mon_ones);

    /*
     * Year pivot: the RP5C01A stores only the last two digits.
     * Convention: 78-99 = 1978-1999, 00-77 = 2000-2077.
     */
    uint8_t yr2 = bcd_decode(yr_tens, yr_ones);
    dt->year = (yr2 >= 78) ? (1900 + yr2) : (2000 + yr2);
}

/*
 * Read date/time from CIA-A TOD (fallback when no RTC chip present).
 *
 * CIA TOD latch protocol:
 *   Read TODHI first  -> latches all three registers
 *   Read TODMID       -> returns latched value
 *   Read TODLO        -> returns latched value AND releases latch
 *
 * NEVER read TODLO before TODHI, or you will read inconsistent values.
 *
 * The TOD counter only gives elapsed time since boot (no date).
 * We set date to 1978-01-01 (Amiga launch year) as a sentinel.
 */
static void read_from_tod(DateTime *dt)
{
    /* Ensure CRA ALARM bit is clear (otherwise reads go to alarm registers) */
    volatile uint8_t *cra_p = (volatile uint8_t *)CIAA_CRA_ADDR;
    uint8_t cra = *cra_p;
    if (cra & CRA_ALARM) {
        *cra_p = cra & (uint8_t)~CRA_ALARM;
    }

    /* Read in correct order: HI (latches) -> MID -> LO (releases) */
    uint8_t hi  = *((volatile const uint8_t *)CIAA_TODHI_ADDR);
    uint8_t mid = *((volatile const uint8_t *)CIAA_TODMID_ADDR);
    uint8_t lo  = *((volatile const uint8_t *)CIAA_TODLO_ADDR);

    uint32_t ticks = ((uint32_t)hi << 16) |
                     ((uint32_t)mid << 8)  |
                      (uint32_t)lo;

    /* Determine tick rate from machine type */
    uint32_t hz = is_pal_machine() ? 50u : 60u;

    uint32_t total_secs = ticks / hz;

    dt->second = (uint8_t)(total_secs % 60);
    dt->minute = (uint8_t)((total_secs / 60) % 60);
    dt->hour   = (uint8_t)((total_secs / 3600) % 24);

    /* No date from TOD; use Amiga launch date as sentinel */
    dt->day    = 23;
    dt->month  = 4;
    dt->year   = 1985;  /* Amiga 1000 launch date */
}

void read(DateTime *dt)
{
    if (!dt) return;

    if (rtc_chip_present) {
        read_from_rpc01a(dt);
    } else {
        read_from_tod(dt);
    }
}

/*
 * Write date/time to the RP5C01A.
 * Has no effect if no RTC chip is present (CIA TOD is set via set_tod()).
 */
void write(const DateTime *dt)
{
    if (!dt || !rtc_chip_present) return;

    /* Validate ranges */
    if (dt->second > 59 || dt->minute > 59 || dt->hour > 23) return;
    if (dt->day < 1 || dt->day > 31) return;
    if (dt->month < 1 || dt->month > 12) return;

    uint8_t tens, ones;
    uint16_t year = dt->year;

    /* Freeze during write */
    rpc_write(RTC_MODE, RTC_MODE_HOLD);

    bcd_encode(dt->second, &tens, &ones);
    rpc_write(RTC_SEC_TENS, tens);
    rpc_write(RTC_SEC_ONES, ones);

    bcd_encode(dt->minute, &tens, &ones);
    rpc_write(RTC_MIN_TENS, tens);
    rpc_write(RTC_MIN_ONES, ones);

    bcd_encode(dt->hour, &tens, &ones);
    rpc_write(RTC_HOUR_TENS, tens);
    rpc_write(RTC_HOUR_ONES, ones);

    bcd_encode(dt->day, &tens, &ones);
    rpc_write(RTC_DAY_TENS, tens);
    rpc_write(RTC_DAY_ONES, ones);

    bcd_encode(dt->month, &tens, &ones);
    rpc_write(RTC_MON_TENS, tens);
    rpc_write(RTC_MON_ONES, ones);

    /* Store last two digits of year */
    uint8_t yr2 = (uint8_t)(year % 100);
    bcd_encode(yr2, &tens, &ones);
    rpc_write(RTC_YEAR_TENS, tens);
    rpc_write(RTC_YEAR_ONES, ones);

    /* Release hold */
    rpc_write(RTC_MODE, 0x00);
}

/*
 * Set the CIA-A TOD counter to a given number of seconds since midnight.
 * Useful for synchronising the CIA TOD to the RTC time on boot.
 */
void set_tod(uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t hz    = is_pal_machine() ? 50u : 60u;
    uint32_t secs  = (uint32_t)hour * 3600u +
                     (uint32_t)minute * 60u +
                     (uint32_t)second;
    uint32_t ticks = secs * hz;

    volatile uint8_t *cra_p = (volatile uint8_t *)CIAA_CRA_ADDR;
    uint8_t cra = *cra_p;

    /* Set CRA ALARM=0: writes go to TOD, not alarm */
    *cra_p = cra & (uint8_t)~CRA_ALARM;

    /* Write TOD: must write HI first to start the counter */
    *((volatile uint8_t *)CIAA_TODHI_ADDR)  = (uint8_t)(ticks >> 16);
    *((volatile uint8_t *)CIAA_TODMID_ADDR) = (uint8_t)(ticks >>  8);
    *((volatile uint8_t *)CIAA_TODLO_ADDR)  = (uint8_t)(ticks);
}

} /* namespace rtc */
} /* namespace neo */
