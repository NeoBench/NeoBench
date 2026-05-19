; NeoBoot Launcher v1.2 (Standard CLI Header)
    SECTION code,CODE

SysBase     EQU 4
_LVOOpen    EQU -30
_LVOClose   EQU -36
_LVORead    EQU -42
_LVOOldOpenLibrary EQU -408
_LVOAllocMem EQU -198
_LVODisable  EQU -120
_LVOForbid   EQU -132

; Standard Amiga DOS Command Header
start:
    move.l  SysBase,a6
    lea     dosname(pc),a1
    moveq   #0,d0
    jsr     _LVOOldOpenLibrary(a6)
    move.l  d0,d6
    beq.s   exit
    
    ; Open neobench.bin
    move.l  d6,a6
    lea     path(pc),a1
    move.l  a1,d1
    move.l  #1005,d2            ; MODE_OLDFILE
    jsr     _LVOOpen(a6)
    move.l  d0,d7
    beq.s   exit

    ; Allocate memory
    move.l  SysBase,a6
    move.l  #524288,d0
    move.l  #2,d1               ; MEMF_CHIP
    jsr     _LVOAllocMem(a6)
    move.l  d0,a5
    beq.s   exit
    
    ; Read and Close
    move.l  d6,a6
    move.l  d7,d1
    move.l  a5,d2
    move.l  #524288,d3
    jsr     _LVORead(a6)
    move.l  d7,d1
    jsr     _LVOClose(a6)
    
    ; Takeover
    move.l  SysBase,a6
    jsr     _LVOForbid(a6)
    jsr     _LVODisable(a6)
    
    move.l  #$dff000,a0
    move.w  #$7fff,96(a0)      ; Kill DMA
    move.w  #$7fff,$9a(a0)     ; Kill INT
    
    ; Execute Kernel
    move.l  4(a5),a0
    jmp     (a0)

exit:
    moveq   #0,d0
    rts

dosname:    dc.b "dos.library",0
    even
path:       dc.b "NeoBench:NeoBench/neobench.bin",0
    even
