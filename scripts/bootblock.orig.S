| NeoBench hard-disk partition bootblock (68000). ORIGINAL (pre-probe).
|
| Assembled to a flat binary with m68k-elf binutils and inserted at
| offset $0C of the 1024-byte bootblock.
|
| Entry register state (per Amiga RKM / strap):
|   A0 = address of the 1024-byte bootblock buffer
|   A1 = address of the boot device's IOStdReq (already open)
|   A6 = SysBase, D0 = 0
|
| Assemble-time constants (--defsym):
|   KERNEL_LBA      absolute block number of the kernel data on disk
|   KERNEL_SECTORS  length of the kernel in 512-byte blocks
|   LOAD_ADDR       flat load address of the kernel image
|   ENTRY           runtime address of the kernel entry point


        .text
        .globl _start

| -------------------------------------------------------------------
| IOStdReq field offsets (classic m68k AmigaOS binary layout).
| -------------------------------------------------------------------
IO_DEVICE  = 20
IO_COMMAND = 28
IO_ERROR   = 31
IO_LENGTH  = 36
IO_DATA    = 40
IO_OFFSET  = 44

CMD_READ = 2
CHUNK    = 256          | sectors per DoIO (128 KB; safe for scsi.device)

LVO_OpenDevice = -552
LVO_DoIO       = -624

_start:
        move.w  #0x0BB8,0x00dff032   | FS-UAE beacon: proves bootblock ran
        move.w  #0x2700,%sr          | supervisor mode, interrupts off
        move.l  %a0,%a2              | keep bootblock base

        lea     banner(%pc),%a0
        bsr.w    print

        | Use an IOStdReq opened against scsi.device (unit 0).
        | We always open it ourselves: when this bootblock runs from the
        | floppy, the request Kickstart hands us in A1 belongs to
        | trackdisk.device, and the kernel lives on the IDE disk.
        lea     0x300(%a2),%a1         | scratch IOStdReq inside our buffer
        lea     devname(%pc),%a0
        moveq   #0,%d0                 | unit 0
        move.l  4.w,%a6
        jsr     LVO_OpenDevice(%a6)
        bne.w    fail

        req_ready:
        move.w  #CMD_READ,IO_COMMAND(%a1)
        move.l  #KERNEL_LBA,%d6        | current absolute block
        move.l  #KERNEL_SECTORS,%d3    | sectors remaining
        move.l  #LOAD_ADDR,%a2         | current load pointer

loop:
        tst.l   %d3
        beq.s   done

        move.l  #CHUNK,%d1             | this chunk = min(CHUNK, remaining)
        cmp.l   %d3,%d1
        ble.s   have_chunk
        move.l  %d3,%d1
have_chunk:
        move.l  %d1,%d4                | d4 = byte length of this chunk
        lsl.l   #8,%d4                 | * 256
        add.l   %d4,%d4                | * 2  (together * 512)
        move.l  %d4,IO_LENGTH(%a1)     | io_Length (bytes)
        move.l  %a2,IO_DATA(%a1)       | io_Data
        move.l  %d6,%d5
        lsl.l   #8,%d5                 | * 256
        add.l   %d5,%d5                | * 2  (together * 512)
        move.l  %d5,IO_OFFSET(%a1)     | io_Offset (byte offset)
        move.l  4.w,%a6
        jsr     LVO_DoIO(%a6)
        tst.b   IO_ERROR(%a1)
        bne.w    fail

        add.l   %d1,%d6                | advance LBA
        sub.l   %d1,%d3                | sectors remaining
        add.l   %d4,%a2                | advance destination
        bra.s   loop

done:
        lea     okmsg(%pc),%a0
        bsr.w    print
        move.l  #ENTRY,%a0
        move.w  #0x2700,%sr
        jmp     (%a0)

fail:
        lea     failmsg(%pc),%a0
        bsr.w    print
1:      bra.s   1b                     | diagnostic halt

| -------------------------------------------------------------------
| print: serial output of NUL-terminated string at A0 via the 8520 SDR.
| No handshake: each byte is paced by a short CPU delay so the shift
| register has time to drain on a default-clock serial port.
| -------------------------------------------------------------------
print:
        move.b  (%a0)+,%d0
        beq.s   print_done
        move.b  %d0,0x00bfd100         | CIAB SDR
        move.w  #0x1000,%d1
1:      dbf     %d1,1b
        bra.s   print
print_done:
        rts

| -------------------------------------------------------------------
| Strings
| -------------------------------------------------------------------
banner:
        .ascii  "NB:NeoBench v1.0\r\n"
        .byte   0
okmsg:
        .ascii  "OK\r\n"
        .byte   0
failmsg:
        .ascii  "ERR\r\n"
        .byte   0
devname:
        .ascii  "scsi.device"
        .byte   0