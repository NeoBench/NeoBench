    INCLUDE "boot/hwdefs.inc"

    SECTION .text,CODE
    XREF _kernel_main
    XREF _bss_start
    XREF _bss_end
    
    XDEF _start
    XDEF _cpu_type
    
_start:
    move.w  #$2700,sr
    
    ; Disable all Amiga DMA and interrupts
    move.w  #$7FFF,CUSTOM+DMACON
    move.w  #$7FFF,CUSTOM+INTENA
    move.w  #$7FFF,CUSTOM+INTREQ
    
    ; Minimal wait
    moveq   #10,d0
.wait_settle:
    dbf     d0,.wait_settle

_detect_cpu:
    ; Set VBR early
    lea     $10000,a0
    movec   a0,vbr
    
    ; Hijack Vector 4 and 11
    move.l  16(a0),d1
    move.l  44(a0),d2
    move.l  #_cpu_probe_fail,16(a0)
    move.l  #_cpu_probe_fail,44(a0)
    
    move.l  #30,_cpu_type

    ; Try 68060
    move.l  #60,_probe_type
    DC.L   $4E7A0808              ; movec pcr,d0
    move.l  #60,_cpu_type
    bra     _cpu_detect_done

_cpu_probe_040:
    ; Try 68040
    move.l  #40,_probe_type
    DC.L   $4E7A0004              ; movec itt0,d0
    move.l  #40,_cpu_type
    bra     _cpu_detect_done

_cpu_probe_fail:
    addq.l  #8,sp
    move.l  _probe_type,d0
    cmp.l   #60,d0
    beq     _cpu_probe_040
    move.l  #30,_cpu_type

_cpu_detect_done:
    ; Restore vectors
    lea     $10000,a0
    move.l  d1,16(a0)
    move.l  d2,44(a0)

_setup_stack:
    move.l  #$10000,sp

_clear_bss:
    lea     _bss_start,a0
    lea     _bss_end,a1
    moveq   #0,d0
.bss_loop:
    cmp.l   a1,a0
    bge     .bss_done
    move.l  d0,(a0)+
    bra     .bss_loop
.bss_done:

_jump_to_kernel:
    ; 5 arguments
    pea     0                       ; FPU
    pea     0                       ; Fast Size
    pea     0                       ; Fast Base
    pea     $200000                ; Chip Size
    move.l  _cpu_type,-(sp)
    
    jsr     _kernel_main

_halt:
    stop    #$2700
    bra     _halt

    SECTION .data,DATA
_cpu_type:     DC.L 30
_probe_type:   DC.L 0
