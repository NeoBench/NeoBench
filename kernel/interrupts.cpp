/*
 * NeoBench Bare-Metal Amiga Kernel
 * Interrupt Handler Management
 *
 * Amiga autovector interrupts (levels 1-7):
 *   Level 1: TBE, DSKBLK, SOFTINT
 *   Level 2: PORTS (CIA-A / keyboard)
 *   Level 3: COPER, VERTB (VBlank), BLIT
 *   Level 4: AUD0-AUD3
 *   Level 5: RBF, DSKSYN
 *   Level 6: EXTER (CIA-B)
 *   Level 7: NMI
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace intr {

/* Custom chip registers */
static constexpr uint32 CUSTOM  = 0xDFF000;
static constexpr uint32 INTENA  = CUSTOM + 0x09A;
static constexpr uint32 INTREQ  = CUSTOM + 0x09C;
static constexpr uint32 INTENAR = CUSTOM + 0x01C;
static constexpr uint32 INTREQR = CUSTOM + 0x01E;

/* CIA addresses */
static constexpr uint32 CIAA_BASE = 0xBFE001;
static constexpr uint32 CIAB_BASE = 0xBFD000;
static constexpr uint32 CIA_ICR   = 0xD00;

/* INTENA/INTREQ bits */
static constexpr uint16 INTF_SETCLR  = 0x8000;
static constexpr uint16 INTF_INTEN   = 0x4000;
static constexpr uint16 INTF_EXTER   = 0x2000;
static constexpr uint16 INTF_DSKSYN  = 0x1000;
static constexpr uint16 INTF_RBF     = 0x0800;
static constexpr uint16 INTF_AUD3    = 0x0400;
static constexpr uint16 INTF_AUD2    = 0x0200;
static constexpr uint16 INTF_AUD1    = 0x0100;
static constexpr uint16 INTF_AUD0    = 0x0080;
static constexpr uint16 INTF_BLIT    = 0x0040;
static constexpr uint16 INTF_VERTB   = 0x0020;
static constexpr uint16 INTF_COPER   = 0x0010;
static constexpr uint16 INTF_PORTS   = 0x0008;
static constexpr uint16 INTF_SOFTINT = 0x0004;
static constexpr uint16 INTF_DSKBLK  = 0x0002;
static constexpr uint16 INTF_TBE     = 0x0001;

static constexpr uint32 MAX_HANDLERS = 16;

typedef void (*IntHandler)(uint32 level, void* data);

struct HandlerEntry {
    IntHandler handler;
    void*      data;
    uint16     int_bit;
    bool       active;
};

static HandlerEntry handlers[MAX_HANDLERS];
static volatile uint32 tick_count   = 0;
static volatile uint32 vblank_count = 0;

/* ------------------------------------------------------------------ */
/*  Hardware access                                                    */
/* ------------------------------------------------------------------ */

static inline void    hw16w(uint32 a, uint16 v) { *reinterpret_cast<volatile uint16*>(a) = v; }
static inline uint16  hw16r(uint32 a)            { return *reinterpret_cast<volatile uint16*>(a); }
static inline uint8   hw8r(uint32 a)             { return *reinterpret_cast<volatile uint8*>(a); }

/* ------------------------------------------------------------------ */
/*  IPL manipulation                                                   */
/* ------------------------------------------------------------------ */

void disable()
{
    __asm__ volatile ("or.w #0x0700, %%sr" : : : "cc");
}

void enable()
{
    __asm__ volatile ("and.w #0xF8FF, %%sr" : : : "cc");
}

uint16 disable_save()
{
    uint16 old;
    __asm__ volatile (
        "move.w %%sr, %0\n\t"
        "or.w   #0x0700, %%sr\n\t"
        : "=d"(old) : : "cc"
    );
    return old;
}

void restore(uint16 sr)
{
    __asm__ volatile ("move.w %0, %%sr" : : "d"(sr) : "cc");
}

/* ------------------------------------------------------------------ */
/*  Handler registration                                               */
/* ------------------------------------------------------------------ */

int32 set_handler(uint16 int_bit, IntHandler handler, void* data)
{
    for (uint32 i = 0; i < MAX_HANDLERS; i++) {
        if (!handlers[i].active) {
            handlers[i].handler = handler;
            handlers[i].data    = data;
            handlers[i].int_bit = int_bit;
            handlers[i].active  = true;
            hw16w(INTENA, INTF_SETCLR | int_bit);
            return static_cast<int32>(i);
        }
    }
    return -1;
}

void remove_handler(int32 slot)
{
    if (slot < 0 || static_cast<uint32>(slot) >= MAX_HANDLERS) return;
    if (!handlers[slot].active) return;
    hw16w(INTENA, handlers[slot].int_bit);
    handlers[slot].active  = false;
    handlers[slot].handler = nullptr;
}

/* ------------------------------------------------------------------ */
/*  Dispatch                                                           */
/* ------------------------------------------------------------------ */

static void dispatch(uint16 bits)
{
    for (uint32 i = 0; i < MAX_HANDLERS; i++)
        if (handlers[i].active && (bits & handlers[i].int_bit))
            handlers[i].handler(0, handlers[i].data);
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

void init()
{
    disable();

    for (uint32 i = 0; i < MAX_HANDLERS; i++) {
        handlers[i].handler = nullptr;
        handlers[i].data    = nullptr;
        handlers[i].int_bit = 0;
        handlers[i].active  = false;
    }

    tick_count = 0;
    vblank_count = 0;

    /* Disable & clear all Amiga interrupts */
    hw16w(INTENA, 0x7FFF);
    hw16w(INTREQ, 0x7FFF);

    /* Clear CIA ICRs */
    (void)hw8r(CIAA_BASE + CIA_ICR);
    (void)hw8r(CIAB_BASE + CIA_ICR);

    /* Enable master + VBlank + PORTS (CIA-A) + EXTER (CIA-B) */
    hw16w(INTENA, INTF_SETCLR | INTF_INTEN | INTF_VERTB | INTF_PORTS | INTF_EXTER);
}

uint32 get_ticks()       { return tick_count; }
uint32 get_vblank_count(){ return vblank_count; }

}  /* namespace intr */
}  /* namespace neo */

/* ------------------------------------------------------------------ */
/*  C-linkage handlers called from vectors.S                           */
/* ------------------------------------------------------------------ */

extern "C" {

using namespace neo::intr;

void _int_level1()
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_TBE | INTF_DSKBLK | INTF_SOFTINT);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level2()
{
    uint16 req = hw16r(INTREQR);
    if (req & INTF_PORTS) {
        /* Dispatch handlers BEFORE reading CIA-A ICR so that handlers (e.g.
         * tick_handler) can read ICR themselves to identify which CIA-A
         * source fired (Timer A = bit 0, keyboard = bit 3, etc.).
         * The ICR read below then clears the flags to re-arm the CIA. */
        dispatch(INTF_PORTS);
        (void)hw8r(CIAA_BASE + CIA_ICR);  /* acknowledge: clear CIA-A ICR flags */
        hw16w(INTREQ, INTF_PORTS);
    }
}

void _int_level3()
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

void _int_level4()
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level5()
{
    uint16 active = hw16r(INTREQR) & hw16r(INTENAR) & (INTF_RBF | INTF_DSKSYN);
    if (active) { dispatch(active); hw16w(INTREQ, active); }
}

void _int_level6()
{
    uint16 req = hw16r(INTREQR);
    if (req & INTF_EXTER) {
        (void)hw8r(CIAB_BASE + CIA_ICR);
        dispatch(INTF_EXTER);
        hw16w(INTREQ, INTF_EXTER);
    }
}

}  /* extern "C" */
