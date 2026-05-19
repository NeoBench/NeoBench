/*
 * NeoBench Bare-Metal Amiga Kernel
 * FPU Initialization and Context Management
 *
 * Supports 68030+68881/882, 68040, 68060 (internal FPU).
 * Context save/restore for cooperative multitasking.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace fpu {

/*
 * FPU state frame sizes differ by CPU:
 *   68881:  idle=4, busy=up to 184 bytes
 *   68882:  idle=4, busy=up to 216 bytes
 *   68040:  idle=4, unimp=44, busy=96
 *   68060:  idle=4, excp=12, busy=12
 *
 * We allocate the maximum possible for compatibility.
 */
static constexpr uint32 FPU_FRAME_MAX  = 216;
static constexpr uint32 FPU_REGS_SIZE  = 96;   /* 8 x 12 bytes (80-bit extended + padding) */

/* FPU context block - must be long-aligned */
struct FPUContext {
    uint8  frame[FPU_FRAME_MAX];    /* FSAVE/FRESTORE internal state frame */
    uint32 fpcr;                     /* FP control register */
    uint32 fpsr;                     /* FP status register */
    uint32 fpiar;                    /* FP instruction address register */
    uint8  fpregs[FPU_REGS_SIZE];   /* FP0-FP7 extended precision */
} __attribute__((aligned(4)));

static uint32 fpu_type    = 0;   /* 0=none, 68881, 68882, 68040, 68060 */
static bool   fpu_present = false;

/* ------------------------------------------------------------------ */
/*  Initialization                                                     */
/* ------------------------------------------------------------------ */

void init()
{
    extern volatile uint32 _g_fpu_type;

    fpu_type = _g_fpu_type;

    if (fpu_type == 0) {
        fpu_present = false;
        return;
    }
    fpu_present = true;

    /*
     * FPCR layout:
     *   Bits 7-6: Rounding mode (00=nearest, 01=zero, 10=-inf, 11=+inf)
     *   Bits 5-4: Rounding precision (00=extended, 01=single, 10=double)
     *   Bits 15-8: Exception enable mask
     *
     * We set double precision, round-to-nearest, no exceptions.
     */
    uint32 fpcr_val = 0x0020;  /* Double precision, round-to-nearest */

    __asm__ volatile (
        "fmove.l  %0, %%fpcr\n\t"
        "fmove.l  #0, %%fpsr\n\t"
        "fmove.l  #0, %%fpiar\n\t"
        : : "d"(fpcr_val) : "memory"
    );

    /* Clear all FP data registers to zero */
    __asm__ volatile (
        "fmove.s  #0, %%fp0\n\t"
        "fmove.s  #0, %%fp1\n\t"
        "fmove.s  #0, %%fp2\n\t"
        "fmove.s  #0, %%fp3\n\t"
        "fmove.s  #0, %%fp4\n\t"
        "fmove.s  #0, %%fp5\n\t"
        "fmove.s  #0, %%fp6\n\t"
        "fmove.s  #0, %%fp7\n\t"
        : : : "memory"
    );
}

/* ------------------------------------------------------------------ */
/*  Context save/restore for task switching                            */
/* ------------------------------------------------------------------ */

void save_context(void* ctx_ptr)
{
    if (!fpu_present || ctx_ptr == nullptr) return;

    FPUContext* ctx = static_cast<FPUContext*>(ctx_ptr);

    /* Save FP control registers */
    __asm__ volatile (
        "fmove.l  %%fpcr, %0\n\t"
        "fmove.l  %%fpsr, %1\n\t"
        "fmove.l  %%fpiar, %2\n\t"
        : "=m"(ctx->fpcr), "=m"(ctx->fpsr), "=m"(ctx->fpiar)
        : : "memory"
    );

    /* Save FP data registers FP0-FP7 */
    __asm__ volatile (
        "fmovem.x %%fp0-%%fp7, %0\n\t"
        : "=m"(ctx->fpregs)
        : : "memory"
    );

    /* FSAVE: save internal FPU state machine */
    __asm__ volatile (
        "fsave %0\n\t"
        : "=m"(ctx->frame)
        : : "memory"
    );
}

void restore_context(const void* ctx_ptr)
{
    if (!fpu_present || ctx_ptr == nullptr) return;

    const FPUContext* ctx = static_cast<const FPUContext*>(ctx_ptr);

    /* FRESTORE: restore internal FPU state (must be done first) */
    __asm__ volatile (
        "frestore %0\n\t"
        : : "m"(ctx->frame) : "memory"
    );

    /* Restore FP data registers FP0-FP7 */
    __asm__ volatile (
        "fmovem.x %0, %%fp0-%%fp7\n\t"
        : : "m"(ctx->fpregs) : "memory"
    );

    /* Restore FP control registers */
    __asm__ volatile (
        "fmove.l  %0, %%fpcr\n\t"
        "fmove.l  %1, %%fpsr\n\t"
        "fmove.l  %2, %%fpiar\n\t"
        : : "m"(ctx->fpcr), "m"(ctx->fpsr), "m"(ctx->fpiar) : "memory"
    );
}

/* ------------------------------------------------------------------ */
/*  Utility                                                            */
/* ------------------------------------------------------------------ */

bool is_present()
{
    return fpu_present;
}

uint32 get_type()
{
    return fpu_type;
}

uint32 get_context_size()
{
    return sizeof(FPUContext);
}

void init_context(void* ctx_ptr)
{
    if (ctx_ptr == nullptr) return;

    FPUContext* ctx = static_cast<FPUContext*>(ctx_ptr);

    /* Zero the entire context */
    uint8* p = reinterpret_cast<uint8*>(ctx);
    for (uint32 i = 0; i < sizeof(FPUContext); i++) {
        p[i] = 0;
    }

    /* Default FPCR: double precision, round to nearest */
    ctx->fpcr = 0x0020;  /* Double precision, round-to-nearest */

    /*
     * NULL FSAVE frame: a single long of 0x00000000.
     * FRESTORE with a NULL frame resets the FPU to idle state.
     */
    ctx->frame[0] = 0x00;
    ctx->frame[1] = 0x00;
    ctx->frame[2] = 0x00;
    ctx->frame[3] = 0x00;
}

}  /* namespace fpu */
}  /* namespace neo */
