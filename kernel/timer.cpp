/*
 * NeoBench Bare-Metal Amiga Kernel
 * CIA-based Timer Driver
 *
 * CIA-A Timer A: System tick (50Hz PAL / 60Hz NTSC)
 * CIA-A Timer B: General purpose
 * CIA-B Timer A/B: Profiling
 *
 * CIA clock = E clock = 709379 Hz (PAL) or 715909 Hz (NTSC)
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace timer {

/* CIA-A register addresses (directly memory-mapped, odd bytes at 0xBFEx01) */
static constexpr uint32 CIAA = 0xBFE001;

/* CIA-A registers (offsets are *INODE_SIZE due to address line mapping) */
static constexpr uint32 CIAA_TALO  = CIAA + 0x400;  /* Timer A low byte */
static constexpr uint32 CIAA_TAHI  = CIAA + 0x500;  /* Timer A high byte */
static constexpr uint32 CIAA_TBLO  = CIAA + 0x600;  /* Timer B low byte */
static constexpr uint32 CIAA_TBHI  = CIAA + 0x700;  /* Timer B high byte */
static constexpr uint32 CIAA_ICR   = CIAA + 0xD00;  /* Interrupt control register */
static constexpr uint32 CIAA_CRA   = CIAA + 0xE00;  /* Control register A */
static constexpr uint32 CIAA_CRB   = CIAA + 0xF00;  /* Control register B */

/* CIA-B register addresses */
static constexpr uint32 CIAB = 0xBFD000;

static constexpr uint32 CIAB_TALO  = CIAB + 0x400;
static constexpr uint32 CIAB_TAHI  = CIAB + 0x500;
static constexpr uint32 CIAB_TBLO  = CIAB + 0x600;
static constexpr uint32 CIAB_TBHI  = CIAB + 0x700;
static constexpr uint32 CIAB_ICR   = CIAB + 0xD00;
static constexpr uint32 CIAB_CRA   = CIAB + 0xE00;
static constexpr uint32 CIAB_CRB   = CIAB + 0xF00;

/* CIA control register bits */
static constexpr uint8 CIA_CR_START    = 0x01;
static constexpr uint8 CIA_CR_ONESHOT  = 0x08;
static constexpr uint8 CIA_CR_LOAD     = 0x10;

/* CIA ICR bits */
static constexpr uint8 CIA_ICR_TA      = 0x01;  /* Timer A */
static constexpr uint8 CIA_ICR_TB      = 0x02;  /* Timer B */
static constexpr uint8 CIA_ICR_SETCLR  = 0x80;  /* Set/clear */

/* E clock frequencies */
static constexpr uint32 ECLK_PAL  = 709379;
static constexpr uint32 ECLK_NTSC = 715909;

/* Tick rates */
static constexpr uint32 TICK_HZ_PAL  = 50;
static constexpr uint32 TICK_HZ_NTSC = 60;

static uint32 eclk_freq    = ECLK_PAL;
static uint32 tick_hz      = TICK_HZ_PAL;
static volatile uint32 sys_ticks = 0;
static bool is_pal = true;

/* ------------------------------------------------------------------ */
/*  Hardware access                                                    */
/* ------------------------------------------------------------------ */

static inline void    hw8w(uint32 a, uint8 v) { *reinterpret_cast<volatile uint8*>(a) = v; }
static inline uint8   hw8r(uint32 a)           { return *reinterpret_cast<volatile uint8*>(a); }

/* ------------------------------------------------------------------ */
/*  Timer interrupt handler                                            */
/* ------------------------------------------------------------------ */

static void tick_handler(uint32 /*level*/, void* /*data*/)
{
    /* Read CIA-A ICR to determine which CIA-A source fired.
     * This must be done before _int_level2 clears it — achieved by having
     * _int_level2 dispatch handlers first, then read ICR (see interrupts.cpp).
     * CIA_ICR_TA (bit 0) indicates Timer A underflow — the system tick source.
     * Without this check, keyboard/serial/etc. events also increment sys_ticks,
     * inflating get_ticks() and making delay_ms() / get_uptime_seconds() inaccurate. */
    uint8 icr = *reinterpret_cast<volatile uint8*>(0xBFED01);
    if (icr & CIA_ICR_TA) {
        sys_ticks++;
    }
}

/* ------------------------------------------------------------------ */
/*  Public interface                                                    */
/* ------------------------------------------------------------------ */

void init()
{
    /*
     * Detect PAL/NTSC from VPosR (bit 15 of VPOSR at 0xDFF004)
     * PAL: long frame flag set (bit 15 = 1 on PAL Agnus)
     */
    volatile uint16* vposr = reinterpret_cast<volatile uint16*>(0xDFF004);
    uint16 vpos = *vposr;

    if (vpos & 0x1000) {
        /* PAL */
        is_pal    = true;
        eclk_freq = ECLK_PAL;
        tick_hz   = TICK_HZ_PAL;
    } else {
        /* NTSC */
        is_pal    = false;
        eclk_freq = ECLK_NTSC;
        tick_hz   = TICK_HZ_NTSC;
    }

    sys_ticks = 0;

    /* Stop CIA-A Timer A */
    hw8w(CIAA_CRA, 0x00);

    /*
     * Calculate timer reload value for desired tick rate.
     * Timer counts down from reload to 0, then fires interrupt.
     * reload = (eclk_freq / tick_hz) - 1
     */
    uint32 reload = (eclk_freq / tick_hz) - 1;
    uint8 lo = static_cast<uint8>(reload & 0xFF);
    uint8 hi = static_cast<uint8>((reload >> 8) & 0xFF);

    /* Load timer value */
    hw8w(CIAA_TALO, lo);
    hw8w(CIAA_TAHI, hi);

    /* Clear pending interrupts */
    (void)hw8r(CIAA_ICR);

    /* Enable Timer A interrupt on CIA-A */
    hw8w(CIAA_ICR, CIA_ICR_SETCLR | CIA_ICR_TA);

    /* Start Timer A: continuous mode */
    hw8w(CIAA_CRA, CIA_CR_START | CIA_CR_LOAD);

    /* Register our handler for PORTS (CIA-A fires through level 2) */
    /* Note: The interrupt system routes CIA-A through INTF_PORTS (level 2) */
    neo::intr::set_handler(0x0008, tick_handler, nullptr);  /* INTF_PORTS = 0x0008 */

    /* Initialize CIA-B timers for profiling (stopped) */
    hw8w(CIAB_CRA, 0x00);
    hw8w(CIAB_CRB, 0x00);
    /* Set max count for profiling use */
    hw8w(CIAB_TALO, 0xFF);
    hw8w(CIAB_TAHI, 0xFF);
    hw8w(CIAB_TBLO, 0xFF);
    hw8w(CIAB_TBHI, 0xFF);
}

uint32 get_ticks()
{
    return sys_ticks;
}

void delay_ms(uint32 ms)
{
    /*
     * Convert ms to ticks: ticks = ms * tick_hz / 1000
     * Round up to ensure minimum delay.
     */
    uint32 delay_ticks = (ms * tick_hz + 999) / 1000;
    if (delay_ticks == 0) delay_ticks = 1;

    uint32 start = sys_ticks;
    while ((sys_ticks - start) < delay_ticks) {
        /* Halt CPU until next interrupt to save power */
        __asm__ volatile ("stop #0x2000" : : : "memory");
    }
}

uint32 get_uptime_seconds()
{
    return sys_ticks / tick_hz;
}

uint32 get_tick_hz()
{
    return tick_hz;
}

bool get_is_pal()
{
    return is_pal;
}

/* ------------------------------------------------------------------ */
/*  Profiling timer (CIA-B)                                            */
/* ------------------------------------------------------------------ */

void profile_start()
{
    /* Reset CIA-B Timer A to max and start counting down */
    hw8w(CIAB_CRA, 0x00);       /* Stop */
    hw8w(CIAB_TALO, 0xFF);
    hw8w(CIAB_TAHI, 0xFF);
    hw8w(CIAB_CRA, CIA_CR_START | CIA_CR_LOAD);  /* Start, continuous */
}

uint32 profile_stop()
{
    /* Read current CIA-B Timer A value */
    uint8 lo = hw8r(CIAB + 0x400);
    uint8 hi = hw8r(CIAB + 0x500);
    hw8w(CIAB_CRA, 0x00);  /* Stop */

    uint32 remaining = (static_cast<uint32>(hi) << 8) | lo;
    uint32 elapsed = 0xFFFF - remaining;

    /* Convert to microseconds: elapsed_us = elapsed * 1000000 / eclk_freq */
    return (elapsed * 1000) / (eclk_freq / 1000);
}

}  /* namespace timer */
}  /* namespace neo */
