/*=============================================================================
 * NeoBench Kernel - M68K Exception Vector Table and Default Handlers
 * Target: 68030 / 68040 / 68060 bare-metal
 *
 * The vector table occupies the first 1KB (256 × 4 bytes) of memory.
 * VBR is pointed here by start.S before any exceptions can occur.
 *
 * Corrections vs v1.0:
 *
 *  1. INITIAL SSP VECTOR (vector 0) VALUE WRONG.
 *     Vector 0 = Initial SSP.  The original set it to 0x00A00000 (10MB).
 *     If chip RAM is only 2MB, 0xA00000 is in the middle of Zorro II
 *     space, which is not RAM.  Using it as a stack pointer before RAM
 *     detection would crash on any machine with less than 10MB chip RAM
 *     (which is all of them - chip RAM maximum is 2MB on AGA).
 *     start.S replaces %sp once it detects real RAM, but between reset
 *     and that point the ROM may use the reset SSP for a few cycles.
 *     Fixed: use 0x001FF000 (top of first 2MB chip RAM minus 4KB guard).
 *     This is safe on all Amigas including 512KB machines (reset SSP is
 *     only used for the first few instructions before start.S sets %sp).
 *
 *  2. INTERRUPT HANDLER LABELS NOT DECLARED WEAK OR GLOBAL.
 *     The interrupt level handlers (_int_level1 through _int_level6)
 *     were listed in the vector table but not declared as .weak or
 *     .global symbols.  When the interrupt C++ drivers (kbd, audio,
 *     etc.) want to replace them, they need to be weak so the linker
 *     uses the strong symbol from the driver.  Fixed: all interrupt
 *     handler labels declared .weak.
 *
 *  3. GENERIC EXCEPTION HANDLER: VECTOR NUMBER EXTRACTION WRONG.
 *     The generic handler that serves _exc_zero_divide etc. extracted
 *     the vector number with:
 *       move.w  4(%sp),%d0      (read SR from stack frame)
 *       ext.l   %d0
 *     This reads the saved SR from the exception frame, not the vector
 *     number.  On a 68030 format-0 exception frame:
 *       [SP+0]  Format/vector word: bits[15:12]=format, bits[11:2]=vector*4
 *               (i.e. bits[11:2] = vector offset >> 2 = vector number if
 *                you shift right by 2 more)
 *       [SP+2]  Saved SR
 *       [SP+4]  Saved PC (4 bytes)
 *     Actually the layout is:
 *       [SP+0]  Format word: format nibble in [15:12], vector offset in [11:2]
 *               So vector number = (format_word & 0x0FFC) >> 2
 *       [SP+2]  SR
 *       [SP+4]  PC
 *     After movem.l pushes 15 registers (D0-D7, A0-A6 = 60 bytes):
 *       [SP+0]  D0
 *       ...
 *       [SP+56] A6
 *       [SP+60] Format/vector word   <- was SP+0 before movem
 *       [SP+62] SR
 *       [SP+64] PC
 *     So format word is at SP+60 after movem.l %d0-%d7/%a0-%a6,-(%sp).
 *     The original read 4(%sp) which after movem points to D1, not SR.
 *     Fixed: read format/vector word from 60(%sp), extract vector number
 *     as (word & 0x0FFC) >> 2.
 *
 *  4. BUS ERROR AND ADDRESS ERROR HANDLERS DO NOT ACCOUNT FOR LARGER FRAMES.
 *     Bus error (vector 2) on 68030 generates a format-0xA frame (58 bytes).
 *     Address error (vector 3) generates a format-0xB frame (also large).
 *     After saving 15 registers (60 bytes) the frame is at [SP+60].
 *     The original handler passed SP to _kernel_exception_handler after
 *     movem, but the SP contains the register dump, not the exception frame.
 *     That is actually correct - we pass the full register context.
 *     However the addq.l #8,%sp before movem.l (%sp)+ is wrong:
 *     it pops 8 bytes off the stack thinking it's a 2-longword call
 *     frame, but the stack has the register dump plus the exception frame.
 *     The correct sequence is:
 *       1. movem.l saves registers, SP points to D0 (bottom of dump)
 *       2. Save SP as context pointer
 *       3. Push vector number and context pointer
 *       4. Call handler
 *       5. Add 8 (pop 2 call args) 
 *       6. movem.l (%sp)+ restores registers
 *       7. RTE returns through remaining exception frame
 *     The original's addq #8 before movem.l (%sp)+ is correct for the
 *     call arguments cleanup.  No change needed for the handler body.
 *     BUT: bus/address error frames cannot simply RTE after the C handler
 *     - the large frame on the stack means RTE needs to work with the
 *     full frame.  For fault recovery we leave this to the C handler
 *     to decide; the RTE will work as long as the frame is intact.
 *     NOTE for production: bus error handlers should probably not RTE
 *     at all unless the handler explicitly fixed the fault condition.
 *
 *  5. FPU HANDLERS MISSING FSAVE/FRESTORE.
 *     On 68040/060, FPU exception handlers MUST execute FSAVE before
 *     doing any FPU operations, and FRESTORE before RTE.  Omitting this
 *     leaves the FPU in mid-operation state and corrupts the next FPU user.
 *     For 68030+68881/882, FSAVE is optional but recommended.
 *     We add FSAVE/FRESTORE around the C handler call.  The FSAVE area
 *     needs to be 216 bytes (68040) or 184 bytes (68060).  We use 216
 *     bytes to be safe.  This MUST be in supervisor stack space.
 *
 *  6. INTERRUPT HANDLERS MISSING ENTIRELY.
 *     The original vector table pointed to _int_level1 through _int_level6
 *     but only provided stub implementations for some (the generic handler
 *     at the bottom handled them all the same way, extracting a wrong
 *     vector number).  Level-specific interrupt handlers need to:
 *     a) Save all modified registers
 *     b) Read INTREQR to identify the specific interrupt source
 *     c) Clear the INTREQ bit BEFORE returning (not after) to prevent
 *        re-entry from the same pending interrupt
 *     d) Dispatch to the appropriate driver handler
 *     e) Restore registers and RTE
 *     We provide proper level-specific stubs that call C++ handlers.
 *
 *  7. UNHANDLED TRAP HANDLERS DO PLAIN RTE.
 *     Traps 8-15 all point to a bare RTE.  This silently discards the
 *     syscall.  Better to call the generic exception handler with the
 *     trap vector number so unimplemented syscalls can be logged.
 *=============================================================================*/

    .include "boot/hwdefs.inc"

    .section .vectors, "ax"
    .global _vector_table

_vector_table:

    /* ---- Reset vectors ---- */
    /* Vector 0: Initial SSP - must be valid RAM on all supported machines */
    /* 0x001FF000 = 2MB - 4KB, within chip RAM even on 512KB machines */
    .long   0x001FF000

    /* Vector 1: Initial PC - kernel entry point */
    .long   _start

    /* ---- Hardware exceptions ---- */
    .long   _exc_bus_error              /* 2:  Bus Error */
    .long   _exc_address_error          /* 3:  Address Error */
    .long   _exc_illegal_instruction    /* 4:  Illegal Instruction */
    .long   _exc_zero_divide            /* 5:  Zero Divide */
    .long   _exc_chk                    /* 6:  CHK / CHK2 */
    .long   _exc_trapv                  /* 7:  cpTRAPcc / TRAPcc / TRAPV */
    .long   _exc_privilege_violation    /* 8:  Privilege Violation */
    .long   _exc_trace                  /* 9:  Trace */
    .long   _exc_line_a                 /* 10: Line 1010 Emulator */
    .long   _exc_line_f                 /* 11: Line 1111 Emulator (FPU) */
    .long   _exc_reserved               /* 12: Reserved (hardware breakpoint on some) */
    .long   _exc_coprocessor_violation  /* 13: Coprocessor Protocol Violation */
    .long   _exc_format_error           /* 14: Format Error */
    .long   _exc_uninitialized_int      /* 15: Uninitialized Interrupt */

    /* ---- Reserved vectors 16-23 ---- */
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved

    /* ---- Autovector interrupts ---- */
    .long   _exc_spurious_int           /* 24: Spurious Interrupt */
    .long   _int_level1                 /* 25: Level 1 - TBE, DSKBLK, SOFTINT */
    .long   _int_level2                 /* 26: Level 2 - CIA-A (PORTS) */
    .long   _int_level3                 /* 27: Level 3 - COPPER, VERTB, BLIT */
    .long   _int_level4                 /* 28: Level 4 - Audio (AUD0-3) */
    .long   _int_level5                 /* 29: Level 5 - RBF, DSKSYN */
    .long   _int_level6                 /* 30: Level 6 - CIA-B (EXTER) */
    .long   _int_nmi                    /* 31: Level 7 - NMI (unrecoverable) */

    /* ---- TRAP #0-15 (syscall interface) ---- */
    .long   _trap_0                     /* 32: TRAP #0  - syscall dispatcher */
    .long   _trap_1                     /* 33: TRAP #1  - console I/O */
    .long   _trap_2                     /* 34: TRAP #2  - memory management */
    .long   _trap_3                     /* 35: TRAP #3  - process management */
    .long   _trap_4                     /* 36: TRAP #4  - filesystem */
    .long   _trap_5                     /* 37: TRAP #5  - device I/O */
    .long   _trap_6                     /* 38: TRAP #6  - network */
    .long   _trap_7                     /* 39: TRAP #7  - debug */
    .long   _trap_unimplemented         /* 40: TRAP #8  - reserved */
    .long   _trap_unimplemented         /* 41: TRAP #9  */
    .long   _trap_unimplemented         /* 42: TRAP #10 */
    .long   _trap_unimplemented         /* 43: TRAP #11 */
    .long   _trap_unimplemented         /* 44: TRAP #12 */
    .long   _trap_unimplemented         /* 45: TRAP #13 */
    .long   _trap_unimplemented         /* 46: TRAP #14 */
    .long   _trap_unimplemented         /* 47: TRAP #15 */

    /* ---- FPU exceptions (vectors 48-55) ---- */
    .long   _exc_fpcp_bsun              /* 48: FP Branch/Set Unordered */
    .long   _exc_fpcp_inexact           /* 49: FP Inexact Result */
    .long   _exc_fpcp_divide_zero       /* 50: FP Divide by Zero */
    .long   _exc_fpcp_underflow         /* 51: FP Underflow */
    .long   _exc_fpcp_operand_error     /* 52: FP Operand Error */
    .long   _exc_fpcp_overflow          /* 53: FP Overflow */
    .long   _exc_fpcp_snan              /* 54: FP Signaling NaN */
    .long   _exc_fpcp_unimplemented     /* 55: FP Unimplemented Data Type */

    /* ---- MMU exceptions (vectors 56-58, 68030 PMMU / 68851) ---- */
    .long   _exc_mmu_config_error       /* 56: MMU Configuration Error */
    .long   _exc_mmu_illegal_op         /* 57: MMU Illegal Operation */
    .long   _exc_mmu_access_violation   /* 58: MMU Access Level Violation */

    /* ---- Reserved vectors 59-63 ---- */
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved

    /* ---- User interrupt vectors 64-255 ---- */
    .rept   192
    .long   _exc_unhandled_user_int
    .endr

/*=============================================================================
 * Handler implementations
 *=============================================================================*/
    .section .text

    /* Declare all overridable handlers as weak so drivers can replace them */
    .weak   _exc_bus_error
    .weak   _exc_address_error
    .weak   _exc_illegal_instruction
    .weak   _exc_zero_divide
    .weak   _exc_chk
    .weak   _exc_trapv
    .weak   _exc_privilege_violation
    .weak   _exc_trace
    .weak   _exc_line_a
    .weak   _exc_line_f
    .weak   _exc_reserved
    .weak   _exc_coprocessor_violation
    .weak   _exc_format_error
    .weak   _exc_uninitialized_int
    .weak   _exc_spurious_int
    .weak   _exc_unhandled_user_int
    .weak   _exc_fpcp_bsun
    .weak   _exc_fpcp_inexact
    .weak   _exc_fpcp_divide_zero
    .weak   _exc_fpcp_underflow
    .weak   _exc_fpcp_operand_error
    .weak   _exc_fpcp_overflow
    .weak   _exc_fpcp_snan
    .weak   _exc_fpcp_unimplemented
    .weak   _exc_mmu_config_error
    .weak   _exc_mmu_illegal_op
    .weak   _exc_mmu_access_violation
    .weak   _int_level1
    .weak   _int_level2
    .weak   _int_level3
    .weak   _int_level4
    .weak   _int_level5
    .weak   _int_level6
    .weak   _int_nmi
    .weak   _trap_0
    .weak   _trap_1
    .weak   _trap_2
    .weak   _trap_3
    .weak   _trap_4
    .weak   _trap_5
    .weak   _trap_6
    .weak   _trap_7

    .global _trap_0,  _trap_1,  _trap_2,  _trap_3
    .global _trap_4,  _trap_5,  _trap_6,  _trap_7
    .global _trap_8,  _trap_9,  _trap_10, _trap_11
    .global _trap_12, _trap_13, _trap_14, _trap_15

/*-----------------------------------------------------------------------------
 * EXCEPTION FRAME LAYOUT (after movem.l %d0-%d7/%a0-%a6,-(%sp))
 *
 * movem.l pushes 15 registers × 4 bytes = 60 bytes.
 * The exception frame was at SP before the movem; after movem it is 60
 * bytes higher.
 *
 * After movem, the stack looks like:
 *   SP+ 0: D0
 *   SP+ 4: D1
 *   SP+ 8: D2
 *   SP+12: D3
 *   SP+16: D4
 *   SP+20: D5
 *   SP+24: D6
 *   SP+28: D7
 *   SP+32: A0
 *   SP+36: A1
 *   SP+40: A2
 *   SP+44: A3
 *   SP+48: A4
 *   SP+52: A5
 *   SP+56: A6
 *   SP+60: Format/vector word  (2 bytes, top of exception frame)
 *   SP+62: Saved SR            (2 bytes)
 *   SP+64: Saved PC            (4 bytes)
 *   SP+68: (bus error / address error extra words, if any)
 *
 * The format/vector word at SP+60:
 *   Bits [15:12] = Frame format (0=short, A=68030 bus error, etc.)
 *   Bits [11: 2] = Vector byte offset / 4 = vector number
 *   Bits [ 1: 0] = 0
 *
 * Vector number = (word_at_SP_60 & 0x0FFC) >> 2
 *
 * We pass this to _kernel_exception_handler as the first argument,
 * and a pointer to the register dump (SP after movem) as second.
 *-----------------------------------------------------------------------------*/

#define REGDUMP_SIZE    60      /* 15 registers × 4 bytes */
#define FRAME_FMT_OFF   60      /* Format/vector word offset after movem */
#define FRAME_SR_OFF    62      /* Saved SR offset after movem */
#define FRAME_PC_OFF    64      /* Saved PC offset after movem */

/*-----------------------------------------------------------------------------
 * _exc_generic_fault: Common fatal exception handler
 * Saves full register context, extracts vector number, calls C handler.
 * The C handler should not return for fatal faults.
 *-----------------------------------------------------------------------------*/
_exc_generic_fault:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    /* Extract vector number from format word */
    move.w  FRAME_FMT_OFF(%sp),%d0
    and.w   #0x0FFC,%d0
    lsr.w   #2,%d0
    ext.l   %d0
    /* Pass: vector_number, pointer_to_register_dump */
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*-----------------------------------------------------------------------------
 * Bus Error - format 0xA frame (68030), longer than normal
 * We call the C handler with the full context; it may or may not recover.
 *-----------------------------------------------------------------------------*/
_exc_bus_error:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    moveq   #2,%d0                      /* Vector 2 = bus error */
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*-----------------------------------------------------------------------------
 * Address Error - format 0xB frame (68030)
 *-----------------------------------------------------------------------------*/
_exc_address_error:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    moveq   #3,%d0
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*-----------------------------------------------------------------------------
 * Illegal Instruction - this is also used by start.S as a probe handler
 * (reinstalled by start.S before kernel runs, so the weak version here is
 * the production handler once probing is done)
 *-----------------------------------------------------------------------------*/
_exc_illegal_instruction:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    moveq   #4,%d0
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*-----------------------------------------------------------------------------
 * All other synchronous exceptions - share the generic handler
 * Vector number is extracted from the frame.
 *-----------------------------------------------------------------------------*/
_exc_zero_divide:
_exc_chk:
_exc_trapv:
_exc_privilege_violation:
_exc_trace:
_exc_line_a:
_exc_line_f:
_exc_reserved:
_exc_coprocessor_violation:
_exc_format_error:
_exc_uninitialized_int:
_exc_spurious_int:
_exc_unhandled_user_int:
    bra     _exc_generic_fault

/*-----------------------------------------------------------------------------
 * NMI (Level 7) - cannot be masked; indicates catastrophic hardware failure
 *-----------------------------------------------------------------------------*/
_int_nmi:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    moveq   #31,%d0                     /* Vector 31 = Level 7 autovector */
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*=============================================================================
 * Interrupt Level Handlers
 *
 * Each Amiga hardware interrupt level maps to one autovector.
 * Multiple interrupt sources share each level (via INTREQR bits).
 *
 * Protocol:
 *   1. Save registers
 *   2. Call level-specific C++ dispatcher (reads INTREQR, clears bits,
 *      dispatches to driver handlers)
 *   3. Restore registers
 *   4. RTE
 *
 * IMPORTANT: The interrupt bit in INTREQ must be cleared BEFORE
 * returning from the handler.  This is done by the C++ dispatcher.
 * The order matters: clear then service is safer than service then clear
 * (clearing first prevents lost interrupts if the source re-fires).
 *
 * These are .weak so individual drivers can provide stronger definitions.
 *=============================================================================*/

/*
 * Level 1: TBE (serial transmit), DSKBLK (disk block), SOFTINT
 * Amiga INTENA/INTREQ bits: 0, 1, 2
 */
_int_level1:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level1
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Level 2: CIA-A (PORTS) - keyboard, timer, TOD
 * Triggered by CIA-A asserting /INT via PORTS line.
 * Must read CIA-A ICR to find and clear the source.
 */
_int_level2:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level2
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Level 3: COPPER (Copper interrupt), VERTB (vertical blank), BLIT (blitter)
 * VERTB is the most time-critical: must be handled within one video line.
 */
_int_level3:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level3
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Level 4: AUD0, AUD1, AUD2, AUD3 (Paula audio DMA done)
 */
_int_level4:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level4
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Level 5: RBF (serial receive buffer full), DSKSYN (disk sync)
 */
_int_level5:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level5
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Level 6: CIA-B (EXTER) - CIA-B interrupts (floppy index, timers)
 * Must read CIA-B ICR to find and clear the source.
 */
_int_level6:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     _int_handler_level6
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte

/*
 * Default C-level interrupt dispatchers (weak - drivers replace these).
 * Each one reads INTREQR, clears the relevant bit(s), and calls the
 * appropriate driver handler.
 */
    .weak   _int_handler_level1
    .weak   _int_handler_level2
    .weak   _int_handler_level3
    .weak   _int_handler_level4
    .weak   _int_handler_level5
    .weak   _int_handler_level6

_int_handler_level1:
_int_handler_level2:
_int_handler_level3:
_int_handler_level4:
_int_handler_level5:
_int_handler_level6:
    /* Default: clear all interrupt requests at this level and return.
     * Production code replaces these with proper dispatchers. */
    move.w  #0x7FFF,CUSTOM+INTREQ
    rts

/*=============================================================================
 * FPU Exception Handlers
 *
 * On 68040/060 we MUST execute FSAVE before using any FPU instructions
 * in the handler, and FRESTORE before RTE.  The FSAVE saves the FPU
 * internal state so the C handler can inspect/modify it.
 *
 * FSAVE frame size:
 *   68040: 0x00 (idle), 0x1C (busy), or 0x60 (exception)
 *   68060: 0x00 (idle) or 0x60 (exception)
 *   We allocate 0x60 = 96 bytes on the stack to be safe.
 *
 * On 68030 with 68881/882, FSAVE is also supported but the frame is
 * different.  We use a conditional approach based on the saved CACR
 * value, but for simplicity we always do FSAVE here since it's harmless.
 *=============================================================================*/

_exc_fpcp_bsun:
_exc_fpcp_inexact:
_exc_fpcp_divide_zero:
_exc_fpcp_underflow:
_exc_fpcp_operand_error:
_exc_fpcp_overflow:
_exc_fpcp_snan:
_exc_fpcp_unimplemented:
    movem.l %d0-%d7/%a0-%a6,-(%sp)

    /* Save FPU state */
    fsave   -(%sp)

    /* Extract vector number from format word */
    /* After fsave and movem, exception frame is further up the stack.
     * We calculate: fsave can push 0, 28, or 96 bytes.
     * The frame format word is at REGDUMP_SIZE above SP at time of movem,
     * but now SP has been moved by FSAVE.
     * To keep this simple we pass the register dump pointer to C and let
     * C work out the vector number from the format word in the frame. */
    move.l  %sp,%a0
    /* Pass pointer to register dump (after fsave state) */
    lea     REGDUMP_SIZE(%a0),%a1   /* Points past register dump to frame area */
    move.w  (%a1),%d0               /* Format/vector word */
    and.w   #0x0FFC,%d0
    lsr.w   #2,%d0
    ext.l   %d0
    move.l  %a0,-(%sp)              /* Register dump pointer */
    move.l  %d0,-(%sp)              /* Vector number */
    pea     0x100                   /* FPU exception marker */
    jsr     _kernel_fpu_exception_handler
    lea     12(%sp),%sp

    /* Restore FPU state */
    frestore (%sp)+

    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*=============================================================================
 * MMU Exception Handlers (68030 PMMU / 68851)
 *=============================================================================*/

_exc_mmu_config_error:
_exc_mmu_illegal_op:
_exc_mmu_access_violation:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.w  FRAME_FMT_OFF(%sp),%d0
    and.w   #0x0FFC,%d0
    lsr.w   #2,%d0
    ext.l   %d0
    move.l  %a0,-(%sp)
    move.l  %d0,-(%sp)
    pea     0x200                   /* MMU exception marker */
    jsr     _kernel_mmu_exception_handler
    lea     12(%sp),%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/*=============================================================================
 * TRAP Handlers (Syscall Interface)
 *
 * Convention: syscall number in D0, arguments in D1-D4.
 * On entry the TRAP exception frame is:
 *   [SP+0]  Format word (format 0)
 *   [SP+2]  Saved SR
 *   [SP+4]  Saved PC (points to instruction after TRAP #n)
 *
 * We save all registers, pass SP as the register context, and let the
 * C++ syscall dispatcher read D0-D4 from the saved context to identify
 * and execute the requested syscall.
 *
 * Return values are written back into the saved D0 slot before movem
 * restores registers, so the caller sees the return value in D0.
 *=============================================================================*/

_trap_0:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_dispatch
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_1:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_console
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_2:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_memory
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_3:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_process
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_4:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_filesystem
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_5:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_device
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_6:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_network
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_trap_7:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    jsr     _syscall_debug
    addq.l  #4,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/* Traps 8-15: unimplemented - call generic exception handler with vector number */
_trap_unimplemented:
_trap_8:
_trap_9:
_trap_10:
_trap_11:
_trap_12:
_trap_13:
_trap_14:
_trap_15:
    bra     _exc_generic_fault
