/*
 * NeoBench Bare-Metal Amiga Kernel
 * CIA-based Timer Driver (C99)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* CIA-A register addresses (directly memory-mapped, odd bytes at 0xBFEx01) */
#define CIAA 0xBFE001

/* CIA-A registers (offsets are *0x100 due to address line mapping) */
#define CIAA_TALO  (CIAA + 0x400)  /* Timer A low byte */
#define CIAA_TAHI  (CIAA + 0x500)  /* Timer A high byte */
#define CIAA_TBLO  (CIAA + 0x600)  /* Timer B low byte */
#define CIAA_TBHI  (CIAA + 0x700)  /* Timer B high byte */
#define CIAA_ICR   (CIAA + 0xD00)  /* Interrupt control register */
#define CIAA_CRA   (CIAA + 0xE00)  /* Control register A */
#define CIAA_CRB   (CIAA + 0xF00)  /* Control register B */

/* CIA-B register addresses */
#define CIAB 0xBFD000

#define CIAB_TALO  (CIAB + 0x400)
#define CIAB_TAHI  (CIAB + 0x500)
#define CIAB_TBLO  (CIAB + 0x600)
#define CIAB_TBHI  (CIAB + 0x700)
#define CIAB_ICR   (CIAB + 0xD00)
#define CIAB_CRA   (CIAB + 0xE00)
#define CIAB_CRB   (CIAB + 0xF00)

/* CIA control register bits */
#define CIA_CR_START    0x01
#define CIA_CR_ONESHOT  0x08
#define CIA_CR_LOAD     0x10

/* CIA ICR bits */
#define CIA_ICR_TA      0x01  /* Timer A */
#define CIA_ICR_TB      0x02  /* Timer B */
#define CIA_ICR_SETCLR  0x80  /* Set/clear */

/* E clock frequencies */
#define ECLK_PAL  709379
#define ECLK_NTSC 715909

/* Tick rates */
#define TICK_HZ_PAL  50
#define TICK_HZ_NTSC 60

static uint32 eclk_freq    = ECLK_PAL;
static uint32 tick_hz      = TICK_HZ_PAL;
static volatile uint32 sys_ticks = 0;
static uint8 is_pal = 1;

/* Hardware access */
static inline void hw8w(uint32 a, uint8 v) { *(volatile uint8*)a = v; }
static inline uint8 hw8r(uint32 a) { return *(volatile uint8*)a; }

/* External declarations from interrupts.c */
int32 intr_set_handler(uint16 int_bit, void (*handler)(uint32, void*), void* data);

/* Timer interrupt handler */
static void tick_handler(uint32 level, void* data)
{
    /* Read CIA-A ICR to determine source */
    uint8 icr = *(volatile uint8*)0xBFED01;
    if (icr & CIA_ICR_TA) {
        sys_ticks++;
    }
}

void timer_init(void)
{
    volatile uint16* vposr = (volatile uint16*)0xDFF004;
    uint16 vpos = *vposr;

    if (vpos & 0x1000) {
        is_pal    = 1;
        eclk_freq = ECLK_PAL;
        tick_hz   = TICK_HZ_PAL;
    } else {
        is_pal    = 0;
        eclk_freq = ECLK_NTSC;
        tick_hz   = TICK_HZ_NTSC;
    }

    sys_ticks = 0;

    hw8w(CIAA_CRA, 0x00);

    uint32 reload = (eclk_freq / tick_hz) - 1;
    uint8 lo = (uint8)(reload & 0xFF);
    uint8 hi = (uint8)((reload >> 8) & 0xFF);

    hw8w(CIAA_TALO, lo);
    hw8w(CIAA_TAHI, hi);

    (void)hw8r(CIAA_ICR);

    hw8w(CIAA_ICR, CIA_ICR_SETCLR | CIA_ICR_TA);

    hw8w(CIAA_CRA, CIA_CR_START | CIA_CR_LOAD);

    /* INTF_PORTS = 0x0008 */
    intr_set_handler(0x0008, tick_handler, 0);

    hw8w(CIAB_CRA, 0x00);
    hw8w(CIAB_CRB, 0x00);
    hw8w(CIAB_TALO, 0xFF);
    hw8w(CIAB_TAHI, 0xFF);
}

uint32 timer_get_ticks(void)
{
    return sys_ticks;
}

void timer_delay_ms(uint32 ms)
{
    uint32 delay_ticks = (ms * tick_hz + 999) / 1000;
    if (delay_ticks == 0) delay_ticks = 1;

    uint32 start = sys_ticks;
    while ((sys_ticks - start) < delay_ticks) {
        /* vbcc doesn't have a good way for inline stop, but we can call it in ASM if needed */
        /* For now just spin */
    }
}

uint32 timer_get_uptime_seconds(void)
{
    return sys_ticks / tick_hz;
}

uint32 timer_get_tick_hz(void)
{
    return tick_hz;
}

uint8 timer_get_is_pal(void)
{
    return is_pal;
}
