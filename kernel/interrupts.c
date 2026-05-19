/*
 * NeoBench Bare-Metal Amiga Kernel
 * Interrupt Handler Management (C99)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* Custom chip registers */
#define CUSTOM_BASE 0xDFF000
#define INTENA  (CUSTOM_BASE + 0x09A)
#define INTREQ  (CUSTOM_BASE + 0x09C)
#define INTENAR (CUSTOM_BASE + 0x01C)
#define INTREQR (CUSTOM_BASE + 0x01E)

/* CIA addresses */
#define CIAA_BASE 0xBFE001
#define CIAB_BASE 0xBFD000
#define CIA_ICR   0xD00

/* INTENA/INTREQ bits */
#define INTF_SETCLR  0x8000
#define INTF_INTEN   0x4000
#define INTF_EXTER   0x2000
#define INTF_DSKSYN  0x1000
#define INTF_RBF     0x0800
#define INTF_AUD3    0x0400
#define INTF_AUD2    0x0200
#define INTF_AUD1    0x0100
#define INTF_AUD0    0x0080
#define INTF_BLIT    0x0040
#define INTF_VERTB   0x0020
#define INTF_COPER   0x0010
#define INTF_PORTS   0x0008
#define INTF_SOFTINT 0x0004
#define INTF_DSKBLK  0x0002
#define INTF_TBE     0x0001

#define MAX_HANDLERS 16

typedef void (*IntHandler)(uint32 level, void* data);

typedef struct {
    IntHandler handler;
    void*      data;
    uint16     int_bit;
    uint8      active;
} HandlerEntry;

static HandlerEntry handlers[MAX_HANDLERS];
static volatile uint32 tick_count   = 0;
static volatile uint32 vblank_count = 0;

/* Hardware access */
static inline void hw16w(uint32 a, uint16 v) { *(volatile uint16*)a = v; }
static inline uint16 hw16r(uint32 a) { return *(volatile uint16*)a; }
static inline uint8 hw8r(uint32 a) { return *(volatile uint8*)a; }

/* IPL manipulation (To be implemented in ASM or using compiler intrinsics) */
void intr_disable(void);
void intr_enable(void);
uint16 intr_disable_save(void);
void intr_restore(uint16 sr);

int32 intr_set_handler(uint16 int_bit, IntHandler handler, void* data)
{
    for (uint32 i = 0; i < MAX_HANDLERS; i++) {
        if (!handlers[i].active) {
            handlers[i].handler = handler;
            handlers[i].data    = data;
            handlers[i].int_bit = int_bit;
            handlers[i].active  = 1;
            hw16w(INTENA, INTF_SETCLR | int_bit);
            return (int32)i;
        }
    }
    return -1;
}

void intr_remove_handler(int32 slot)
{
    if (slot < 0 || (uint32)slot >= MAX_HANDLERS) return;
    if (!handlers[slot].active) return;
    hw16w(INTENA, handlers[slot].int_bit);
    handlers[slot].active  = 0;
    handlers[slot].handler = 0;
}

static void dispatch(uint16 bits)
{
    for (uint32 i = 0; i < MAX_HANDLERS; i++)
        if (handlers[i].active && (bits & handlers[i].int_bit))
            handlers[i].handler(0, handlers[i].data);
}

void intr_init(void)
{
    intr_disable();

    for (uint32 i = 0; i < MAX_HANDLERS; i++) {
        handlers[i].handler = 0;
        handlers[i].data    = 0;
        handlers[i].int_bit = 0;
        handlers[i].active  = 0;
    }

    tick_count = 0;
    vblank_count = 0;

    hw16w(INTENA, 0x7FFF);
    hw16w(INTREQ, 0x7FFF);

    (void)hw8r(CIAA_BASE + CIA_ICR);
    (void)hw8r(CIAB_BASE + CIA_ICR);

    hw16w(INTENA, INTF_SETCLR | INTF_INTEN | INTF_VERTB | INTF_PORTS | INTF_EXTER);
}

uint32 intr_get_ticks(void)       { return tick_count; }
uint32 intr_get_vblank_count(void){ return vblank_count; }

/* Handlers called from assembly */

void _int_level1(void)
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_TBE | INTF_DSKBLK | INTF_SOFTINT);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level2(void)
{
    uint16 req = hw16r(INTREQR);
    if (req & INTF_PORTS) {
        dispatch(INTF_PORTS);
        (void)hw8r(CIAA_BASE + CIA_ICR);
        hw16w(INTREQ, INTF_PORTS);
    }
}

void _int_level3(void)
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_VERTB | INTF_COPER | INTF_BLIT);
    if (active & INTF_VERTB) {
        tick_count++;
        vblank_count++;
        dispatch(INTF_VERTB);
        hw16w(INTREQ, INTF_VERTB);
    }
    if (active & INTF_COPER) { dispatch(INTF_COPER); hw16w(INTREQ, INTF_COPER); }
    if (active & INTF_BLIT)  { dispatch(INTF_BLIT);  hw16w(INTREQ, INTF_BLIT);  }
}

void _int_level4(void)
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level5(void)
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_RBF | INTF_DSKSYN);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level6(void)
{
    uint16 req = hw16r(INTREQR);
    if (req & INTF_EXTER) {
        (void)hw8r(CIAB_BASE + CIA_ICR);
        dispatch(INTF_EXTER);
        hw16w(INTREQ, INTF_EXTER);
    }
}
