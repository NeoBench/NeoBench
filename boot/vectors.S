/*=============================================================================
 * NeoBench Kernel - M68K Exception Vector Table
 * Target: 68030 / 68040 / 68060 bare-metal
 *
 * The first 1KB of memory holds the 256-entry exception vector table.
 * Vectors 0-1 are SSP and PC (reset vectors).
 * Vectors 2-255 are exception handlers.
 *=============================================================================*/

    .section .vectors, "ax"
    .global _vector_table

_vector_table:
    /* Vector 0: Initial SSP (top of Chip RAM, adjusted at runtime) */
    .long   0x00A00000              /* 10MB default SSP */

    /* Vector 1: Initial PC (entry point) */
    .long   _start

    /* Vector 2: Bus Error */
    .long   _exc_bus_error

    /* Vector 3: Address Error */
    .long   _exc_address_error

    /* Vector 4: Illegal Instruction */
    .long   _exc_illegal_instruction

    /* Vector 5: Zero Divide */
    .long   _exc_zero_divide

    /* Vector 6: CHK Instruction */
    .long   _exc_chk

    /* Vector 7: TRAPV Instruction */
    .long   _exc_trapv

    /* Vector 8: Privilege Violation */
    .long   _exc_privilege_violation

    /* Vector 9: Trace */
    .long   _exc_trace

    /* Vector 10: Line 1010 Emulator (A-Line) */
    .long   _exc_line_a

    /* Vector 11: Line 1111 Emulator (F-Line) */
    .long   _exc_line_f

    /* Vectors 12-14: Reserved */
    .long   _exc_reserved
    .long   _exc_coprocessor_violation
    .long   _exc_format_error

    /* Vector 15: Uninitialized Interrupt */
    .long   _exc_uninitialized_int

    /* Vectors 16-23: Reserved */
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved

    /* Vector 24: Spurious Interrupt */
    .long   _exc_spurious_int

    /* Vectors 25-31: Autovector Interrupts (Amiga hardware) */
    .long   _int_level1             /* Level 1: TBE, DSKBLK, SOFTINT */
    .long   _int_level2             /* Level 2: PORTS (CIA-A) */
    .long   _int_level3             /* Level 3: COPER, VERTB, BLIT */
    .long   _int_level4             /* Level 4: AUD0-AUD3 */
    .long   _int_level5             /* Level 5: RBF, DSKSYN */
    .long   _int_level6             /* Level 6: EXTER (CIA-B) */
    .long   _int_nmi                /* Level 7: NMI */

    /* Vectors 32-47: TRAP #0-15 (System calls) */
    .long   _trap_0                 /* TRAP #0: Syscall dispatcher */
    .long   _trap_1                 /* TRAP #1: Fast console I/O */
    .long   _trap_2                 /* TRAP #2: Memory management */
    .long   _trap_3                 /* TRAP #3: Process management */
    .long   _trap_4                 /* TRAP #4: File system */
    .long   _trap_5                 /* TRAP #5: Device I/O */
    .long   _trap_6                 /* TRAP #6: Network */
    .long   _trap_7                 /* TRAP #7: Debug */
    .long   _trap_8
    .long   _trap_9
    .long   _trap_10
    .long   _trap_11
    .long   _trap_12
    .long   _trap_13
    .long   _trap_14
    .long   _trap_15

    /* Vectors 48-54: FPU Exceptions (68030 w/ 68881/68882, 68040/060 internal) */
    .long   _exc_fpcp_bsun          /* FPCP Branch/Set on Unordered */
    .long   _exc_fpcp_inexact       /* FPCP Inexact Result */
    .long   _exc_fpcp_divide_zero   /* FPCP Divide by Zero */
    .long   _exc_fpcp_underflow     /* FPCP Underflow */
    .long   _exc_fpcp_operand_error /* FPCP Operand Error */
    .long   _exc_fpcp_overflow      /* FPCP Overflow */
    .long   _exc_fpcp_snan          /* FPCP Signaling NaN */

    /* Vector 55: FPCP Unimplemented Data Type (68040/060) */
    .long   _exc_fpcp_unimplemented

    /* Vectors 56-58: MMU Exceptions (68030 specific) */
    .long   _exc_mmu_config_error   /* MMU Configuration Error */
    .long   _exc_mmu_illegal_op     /* MMU Illegal Operation (68851) */
    .long   _exc_mmu_access_violation /* MMU Access Level Violation */

    /* Vectors 59-63: Reserved */
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved
    .long   _exc_reserved

    /* Vectors 64-255: User Interrupt Vectors */
    .rept   192
    .long   _exc_unhandled_user_int
    .endr

/*=============================================================================
 * Default Exception Handlers (weak symbols - kernel overrides these)
 *=============================================================================*/
    .section .text

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
    .weak   _int_nmi
    .weak   _exc_unhandled_user_int

/* Generic fault handler - saves state and calls C handler */
_exc_bus_error:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    moveq   #2,%d0
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_exc_address_error:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    moveq   #3,%d0
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

_exc_illegal_instruction:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    moveq   #4,%d0
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

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
_int_nmi:
_exc_unhandled_user_int:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    move.w  4(%sp),%d0
    ext.l   %d0
    move.l  %d0,-(%sp)
    jsr     _kernel_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/* FPU exception handlers */
_exc_fpcp_bsun:
_exc_fpcp_inexact:
_exc_fpcp_divide_zero:
_exc_fpcp_underflow:
_exc_fpcp_operand_error:
_exc_fpcp_overflow:
_exc_fpcp_snan:
_exc_fpcp_unimplemented:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    pea     0x100                   /* FPU exception marker */
    jsr     _kernel_fpu_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/* MMU exception handlers */
_exc_mmu_config_error:
_exc_mmu_illegal_op:
_exc_mmu_access_violation:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.l  %sp,%a0
    move.l  %a0,-(%sp)
    pea     0x200                   /* MMU exception marker */
    jsr     _kernel_mmu_exception_handler
    addq.l  #8,%sp
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte

/* TRAP handlers - syscall interface */
    .global _trap_0, _trap_1, _trap_2, _trap_3
    .global _trap_4, _trap_5, _trap_6, _trap_7
    .global _trap_8, _trap_9, _trap_10, _trap_11
    .global _trap_12, _trap_13, _trap_14, _trap_15

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

_trap_8:
_trap_9:
_trap_10:
_trap_11:
_trap_12:
_trap_13:
_trap_14:
_trap_15:
    rte
