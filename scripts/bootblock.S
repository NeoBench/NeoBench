| NeoBench floppy bootblock loader - standalone ATA PIO driver.
|
| No reliance on exec, scsi.device, or the strap's CPU mode: this probes
| the on-board IDE controller directly (Gayle A1200 base $DA0000, A4000
| base $DD2020), streams the kernel flat image from the HDF into RAM at
| LOAD_ADDR, and jumps to ENTRY.
|
| Entry register state (per Amiga RKM / strap):
|   A0 = address of the 1024-byte bootblock buffer
|   A1 = boot device IOStdReq (unused here)
|   A6 = SysBase, D0 = 0
|
| Assemble-time constants (--defsym):
|   KERNEL_LBA      absolute block number of the kernel data on disk
|   KERNEL_SECTORS  length of the kernel in 512-byte blocks
|   LOAD_ADDR       flat load address of the kernel image
|   ENTRY           runtime address of the kernel entry point


        .text
        .globl _start

| ATA register byte offsets (base + idx*4)
IDE_DATA   = 0x00
IDE_ERROR  = 0x04
IDE_SECT   = 0x08
IDE_LBALO  = 0x0C
IDE_LBMID  = 0x10
IDE_LBAHI  = 0x14
IDE_DRIVE  = 0x18
IDE_STAT   = 0x1C

STR_BSY    = 7
STR_DRQ    = 3
STR_ERR    = 0

CMD_IDENTIFY = 0xEC
CMD_READ     = 0x20

GAYLE_1200 = 0x00DA0000
GAYLE_4000 = 0x00DD2020

_start:
        move.w  #0x0BB8,0x00dff032   | stage1: bootblock executed
        | (strap runs in user mode on 3.2: never touch SR)
        moveq   #'1',%d0
        bsr.w   putc

        | find a live IDE controller base in A4
        lea     idebases(%pc),%a5
probe:
        move.l  (%a5)+,%d4            | candidate base
        beq.w   fail                  | none left
        move.l  %d4,%a4
        bsr.w   ide_probe
        bne.s   probe
        move.w  #0x0AAA,0x00dff032    | stage2: drive identified
        moveq   #'2',%d0
        bsr.w   putc

        | stream the kernel
        bsr.w   ide_read_stream
        bne.w   fail
        move.w  #0x0777,0x00dff032    | stage3: kernel received
        moveq   #'3',%d0
        bsr.w   putc

        move.l  #ENTRY,%a0
        jmp     (%a0)

| -------------------------------------------------------------------
| putc: transmit a single char (D0) on the Paula serial port (SERDAT).
| Stalls on TBE. Uses only D0/D1; caller regs preserved.
| -------------------------------------------------------------------
putc:
        | wait for transmit buffer empty (SERDATR bit 13 = 0x2000)
1:      move.w  0x00dff018,%d1
        btst    #13,%d1
        beq.s   1b
        move.w  #0x3C00,%d1          | 8-bit framing
        or.w    %d0,%d1
        move.w  %d1,0x00dff030       | SERDAT
        rts

| -------------------------------------------------------------------
| ide_probe: IDENTIFY the master device on base A4.
|   ok: D0 = 0, base valid.   fail: D0 != 0.
| Uses only scratch regs; caller regs preserved.
| -------------------------------------------------------------------
ide_probe:
        movem.l %d0-%d2/%a5,-(%a7)
        move.b  #0x00,IDE_DRIVE(%a4)   | master, CHS mode
        move.b  #CMD_IDENTIFY,IDE_STAT(%a4)
        bsr.w   ide_wait
        bne.s   1f
        | identify data ready: consume 256 words
        move.w  #256,%d1
2:      move.w  IDE_DATA(%a4),%d0
        subq.w  #1,%d1
        bne.s   2b
1:      movem.l (%a7)+,%d0-%d2/%a5
        rts

| -------------------------------------------------------------------
| ide_wait: poll STATUS until BSY clear, then check ERR.
|   ok: D0 = 0.   fail: D0 != 0 (ERR or timeout).
| -------------------------------------------------------------------
ide_wait:
        move.l  #0x04000000,%d2        | ~full poll budget
1:      move.b  IDE_STAT(%a4),%d0
        btst    #STR_BSY,%d0
        bne.s   2f
        btst    #STR_ERR,%d0
        bne.s   3f
        moveq   #0,%d0
        rts
2:      dbf     %d2,1b
3:      moveq   #-1,%d0
        rts

| -------------------------------------------------------------------
| ide_read_stream: read KERNEL_SECTORS sectors from absolute block
| KERNEL_LBA on base A4 to LOAD_ADDR.  Single-sector PIO.
|   ok: D0 = 0, kernel in RAM.   fail: D0 != 0.
| -------------------------------------------------------------------
ide_read_stream:
        movem.l %d0-%d6/%a2/%a3,-(%a7)
        move.l  #KERNEL_LBA,%d6
        move.l  #KERNEL_SECTORS,%d3
        move.l  #LOAD_ADDR,%a2
rs_sec:
        move.b  #1,IDE_SECT(%a4)       | 1 sector
        move.b  %d6,IDE_LBALO(%a4)
        move.l  %d6,%d4
        lsr.l   #8,%d4
        move.b  %d4,IDE_LBMID(%a4)
        lsr.l   #8,%d4
        move.b  %d4,IDE_LBAHI(%a4)
        lsr.l   #8,%d4
        or.b    #0xE0,%d4              | LBA bit + master
        move.b  %d4,IDE_DRIVE(%a4)
        move.b  #CMD_READ,IDE_STAT(%a4)
        bsr.w   ide_wait
        bne.s   rs_fail
        move.b  IDE_STAT(%a4),%d1
        btst    #STR_DRQ,%d1
        beq.s   rs_fail
        move.w  #256,%d1
1:      move.w  IDE_DATA(%a4),%d0
        move.w  %d0,(%a2)+
        subq.w  #1,%d1
        bne.s   1b
        addq.l  #1,%d6
        subq.l  #1,%d3
        bne.s   rs_sec
        moveq   #0,%d0
        bra.s   rs_done
rs_fail:
        moveq   #-1,%d0
rs_done:
        movem.l (%a7)+,%d0-%d6/%a2/%a3
        rts

fail:
        | diagnostic halt: paced alert beacon (prevents log flooding)
        moveq   #'E',%d0
        bsr.w   putc
1:      move.w  #0x0333,0x00dff032
        move.w  #0x0333,0x00dff032
        move.l  #0x00080000,%d0
2:      dbf     %d0,2b
        bra.s   1b

| -------------------------------------------------------------------
| String/data
| -------------------------------------------------------------------
idebases:
        .long   GAYLE_1200
        .long   GAYLE_4000
        .long   0
