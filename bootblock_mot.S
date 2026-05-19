    section .text,code
    public _start
_start:
    dc.b "DOS",0
    dc.l 0
    dc.l 880

    ; Set a safe stack
    lea     $7F000,sp

    ; GREEN - Loader active
    move.l  #$dff180,a0
    move.w  #$00f0,(a0)

    movem.l d0-d7/a0-a6,-(sp)
    
    ; Load Kernel from logical Sector 2 (Offset 1024)
    lea     $10000,a0
    move.l  #1024,$2c(a1)     ; io_Offset
    move.l  #262144,$24(a1)   ; io_Length (256KB)
    move.l  a0,$20(a1)        ; io_Data
    move.w  #2,$1c(a1)         ; io_Command (CMD_READ)
    
    move.l  4,a6
    jsr     -456(a6)          ; DoIO
    
    movem.l (sp)+,d0-d7/a0-a6

    ; BLUE - Handover
    move.l  #$dff180,a0
    move.w  #$000f,(a0)
    
    ; Jump to start of kernel at $10000
    move.l  #$10000,a0
    jmp     (a0)
