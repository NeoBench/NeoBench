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
        moveq   #'P',%d0
        bsr.w   putc
        bsr.w   ide_probe
        bne.s   probe
        move.w  #0x0AAA,0x00dff032    | stage2: drive identified
        moveq   #'S',%d0
        bsr.w   putc
        moveq   #'2',%d0
        bsr.w   putc

        | report which IDE base we are using
        moveq   #'B',%d0               | 'B' + two hex digits = base low 16 bits
        bsr.w   putc
        move.l  %a4,%d5
        move.w  %d5,%d0
        lsr.w   #8,%d0
        and.w   #0x00FF,%d0
        bsr.w   hex2
        move.w  %d5,%d0
        and.w   #0x00FF,%d0
        bsr.w   hex2

        | stream the kernel
        bsr.w   ide_read_stream
        bne.w   fail
        move.w  #0x0777,0x00dff032    | stage3: kernel received
        moveq   #'3',%d0
        bsr.w   putc

        move.l  #ENTRY,%a0
        jmp     (%a0)

| -------------------------------------------------------------------
| hex2: print the low 8 bits of D0 as 2 uppercase hex digits.
| Clobbers D1. Caller regs otherwise preserved.
| -------------------------------------------------------------------
hex2:
        move.w  %d0,%d1
        lsr.w   #4,%d1
        and.w   #0x000F,%d1
        bsr.w   hexd
        move.w  %d0,%d1
        and.w   #0x000F,%d1
        bsr.w   hexd
        rts
hexd:   | print hex digit in D1
        cmp.w   #10,%d1
        bcc.s   1f
        add.w   #'0',%d1
        bra.s   2f
1:      add.w   #('A'-10),%d1
2:      move.w  %d1,%d0
        bsr.w   putc
        rts

| -------------------------------------------------------------------
| hexb: print the low 8 bits of D0 as 2 uppercase hex digits.
| Clobbers D1; preserves D0.
| -------------------------------------------------------------------
hexb:
        movem.l %d0,-(%a7)
        bsr.w   hex2
        movem.l (%a7)+,%d0
        rts

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
| ide_probe: verify a live IDE device on base A4 by reading block 0.
|   ok: D0 = 0, base valid.   fail: D0 != 0.
|   IDENTIFY (0xEC) is avoided: FS-UAE's identify handling can settle
|   into an error state, whereas READ SECTORS (0x20) works reliably.
|   Uses only scratch regs; caller regs preserved.
| -------------------------------------------------------------------
ide_probe:
        movem.l %d0-%d2/%a5,-(%a7)
        move.b  #1,IDE_SECT(%a4)       | 1 sector
        move.b  #0,IDE_LBALO(%a4)
        move.b  #0,IDE_LBMID(%a4)
        move.b  #0,IDE_LBAHI(%a4)
        move.b  #0xE0,IDE_DRIVE(%a4)   | LBA + master
        move.b  #CMD_READ,IDE_STAT(%a4)
        bsr.w   ide_wait
        bne.s   probe_fail
        | drive must assert DRQ (data ready); unmapped memory returns
        | 0x00 with DRQ clear, so this rejects spurious bases (e.g. the
        | A1200 Gayle address on an A4000).
        move.b  IDE_STAT(%a4),%d1
        btst    #STR_DRQ,%d1
        beq.s   probe_fail
        | block ready: consume 256 words
        move.w  #256,%d1
2:      move.w  IDE_DATA(%a4),%d0
        subq.w  #1,%d1
        bne.s   2b
        movem.l (%a7)+,%d0-%d2/%a5
        moveq   #0,%d0
        rts
probe_fail:
        move.b  IDE_STAT(%a4),%d0
        bsr.w   hexb
        move.b  IDE_ERROR(%a4),%d0
        bsr.w   hexb
        movem.l (%a7)+,%d0-%d2/%a5
        moveq   #-1,%d0
        rts

| -------------------------------------------------------------------
| ide_wait: poll STATUS until data-ready (DRQ set) or busy-clear,
|   failing on ERR or timeout.  FS-UAE keeps BSY asserted *and* only
|   delivers DRQ from its hsync-driven IDE event loop, which a tight
|   dbf-only loop starves.  We therefore read the video beam register
|   (VPOSR, changes every scanline) each pass to force the emulator's
|   per-scanline event processing (which is what advances the IDE
|   transfer) to actually run.
|   ok: D0 = 0.   fail: D0 != 0 (ERR or timeout).
| -------------------------------------------------------------------
ide_wait:
        | Time the wait against the video beam: repeatedly sampling VPOSR
        | forces FS-UAE's per-scanline event loop (which is what actually
        | advances the async IDE transfer / sets DRQ) to run.  Abort after
        | ~4096 VPOSR transitions (many frames) if no completion.
        moveq   #0,%d2                  | scanline-transition counter
        move.w  0x00dff004,%d1          | previous beam value
iw1:
        move.b  IDE_STAT(%a4),%d0
        btst    #STR_ERR,%d0
        bne.s   iw3                     | error -> fail
        btst    #STR_DRQ,%d0
        bne.s   iw4                     | data ready -> ok
        btst    #STR_BSY,%d0
        beq.s   iw4                     | busy clear -> ok
        move.w  0x00dff004,%d0          | force scanline processing
        cmp.w   %d1,%d0
        beq.s   iw1                     | same scanline: keep polling
        move.w  %d0,%d1                 | new scanline
        addq.w  #1,%d2
        cmp.w   #0x1000,%d2             | ~4096 scanlines elapsed?
        blo.s   iw1
iw3:
        moveq   #-1,%d0
        rts
iw4:
        moveq   #0,%d0
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
        bne.s   rs_wfail
        move.b  IDE_STAT(%a4),%d1
        btst    #STR_DRQ,%d1
        beq.s   rs_dfail
        move.w  #256,%d1
1:      move.w  IDE_DATA(%a4),%d0
        move.w  %d0,(%a2)+
        subq.w  #1,%d1
        bne.s   1b
        addq.l  #1,%d6
        subq.l  #1,%d3
        bne.s   rs_sec
        movem.l (%a7)+,%d0-%d6/%a2/%a3
        moveq   #0,%d0
        rts
rs_wfail:
        move.l  %d3,%d0
        bsr.w   dump_sector
        moveq   #'W',%d0
        bsr.w   putc
        movem.l (%a7)+,%d0-%d6/%a2/%a3
        moveq   #-1,%d0
        rts
rs_dfail:
        move.l  %d3,%d0
        bsr.w   dump_sector
        moveq   #'D',%d0
        bsr.w   putc
        movem.l (%a7)+,%d0-%d6/%a2/%a3
        moveq   #-1,%d0
        rts

fail:
        | diagnostic: sample the IDE status (and error) registers over time
        | so we can watch whether the drive ever offers DRQ vs erroring.
        | Prints 'E', then up to 20 samples of status,error (2 hex each).
        moveq   #'E',%d0
        bsr.w   putc
        moveq   #20,%d3            | ~20 samples
fs:
        move.l  #0x00100000,%d0    | paced delay between samples
2:      dbf     %d0,2b
        move.b  IDE_STAT(%a4),%d0  | sample status at current IDE base
        bsr.w   hexb
        move.b  IDE_ERROR(%a4),%d0
        bsr.w   hexb
        subq.w  #1,%d3
        bne.s   fs
1:      move.w  #0x0333,0x00dff032
        bra.s   1b

| -------------------------------------------------------------------
| dump_sector: print D0 (remaining-sector counter) as 4 hex digits
| followed by '='. Preserves all registers.
| -------------------------------------------------------------------
dump_sector:
        movem.l %d0-%d2/%a0,-(%a7)
        moveq   #3,%d2          | 4 nibbles
1:      rol.w   #4,%d0
        move.w  %d0,%d1
        and.w   #0x000F,%d1
        cmp.w   #10,%d1
        bcc.s   2f
        add.w   #'0',%d1
        bra.s   3f
2:      add.w   #('A'-10),%d1
3:      move.w  %d1,%d0
        bsr.w   putc
        subq.w  #1,%d2
        bne.s   1b
        moveq   #'=',%d0
        bsr.w   putc
        movem.l (%a7)+,%d0-%d2/%a0
        rts

| -------------------------------------------------------------------
| String/data
| -------------------------------------------------------------------
idebases:
        .long   GAYLE_1200
        .long   GAYLE_4000
        .long   0
