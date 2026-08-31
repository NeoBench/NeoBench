| NeoBench test bootblock (68000).
| Minimal: OpenDevice("serial.device"), Write banner, infinite loop.
| No hard drive, no scsi - isolates bootblock execution + serial path.

IO_DEVICE  = 20
IO_COMMAND = 28
IO_ERROR   = 31
IO_ACTUAL  = 32
IO_LENGTH  = 36
IO_DATA    = 40

CMD_WRITE = 8

LVO_OpenDevice = -552
LVO_DoIO       = -624

        .text
        .globl _start

_start:
        move.w  #0x2700,%sr
        move.w  #0x0BB8,0x00dff032     | Paula SERPER beacon: FS-UAE logs this
        move.l  %a0,%a2                | bootblock base (for scratch space)

        lea     0x300(%a2),%a1         | scratch IOStdReq
        lea     devname(%pc),%a0
        moveq   #0,%d0                 | unit 0
        move.l  4.w,%a6
        jsr     LVO_OpenDevice(%a6)
        bne.s   fail

        move.w  #CMD_WRITE,IO_COMMAND(%a1)
        move.l  #banner_len,%d0
        move.l  %d0,IO_LENGTH(%a1)
        lea     banner(%pc),%a0
        move.l  %a0,IO_DATA(%a1)
        move.l  4.w,%a6
        jsr     LVO_DoIO(%a6)
        bne.s   fail

1:      bra.s   1b                     | halt after successful write

fail:
        lea     failmsg(%pc),%a0
        move.l  %a0,IO_DATA(%a1)
        move.l  #failmsg_len,%d0
        move.l  %d0,IO_LENGTH(%a1)
        move.w  #CMD_WRITE,IO_COMMAND(%a1)
        move.l  4.w,%a6
        jsr     LVO_DoIO(%a6)
1:      bra.s   1b

banner:
        .ascii  "NB:TEST\0"
banner_len = . - banner
failmsg:
        .ascii  "NB:FAILED\0"
failmsg_len = . - failmsg
devname:
        .ascii  "serial.device"
        .byte   0